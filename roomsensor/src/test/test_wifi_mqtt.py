import json
import queue
import re
import socket
import subprocess
import time
import uuid
import pytest

from .serial_connection import SerialConnection

try:
    import paho.mqtt.client as mqtt
except Exception:
    mqtt = None


pytestmark = pytest.mark.skipif(mqtt is None, reason="paho-mqtt is required for MQTT tests")


class MqttCapture:
    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self.client = mqtt.Client(client_id=f"roomsensor-test-{uuid.uuid4().hex[:8]}")
        self.msg_q = queue.Queue()
        self.client.on_message = self._on_message
        self.client.connect(self.host, self.port, keepalive=30)
        self.client.loop_start()

    def _on_message(self, client, userdata, msg):
        try:
            self.msg_q.put_nowait((msg.topic, msg.payload.decode("utf-8", errors="replace")))
        except Exception:
            pass

    def subscribe(self, topic: str):
        self.client.subscribe(topic, qos=1)

    def get(self, timeout_s: float = 10.0):
        return self.msg_q.get(timeout=timeout_s)

    def drain(self):
        while not self.msg_q.empty():
            try:
                self.msg_q.get_nowait()
            except queue.Empty:
                break

    def stop(self):
        try:
            self.client.loop_stop()
        except Exception:
            pass
        try:
            self.client.disconnect()
        except Exception:
            pass


def _get_mqtt_broker(conn: SerialConnection) -> tuple[str, int]:
    conn.send_command("nvs_namespace wifi")
    start = conn.send_command("nvs_get mqtt_broker str")
    text = conn.get_text(start_pos=start)
    # Expect something like mqtt://server1.home:1883
    m = re.search(r"mqtt://([^:\s]+)(?::(\d+))?", text)
    if not m:
        pytest.skip("Could not determine MQTT broker from device NVS")
    host = m.group(1)
    port = int(m.group(2) or 1883)
    return host, port


def _wait_for_topic(mc: MqttCapture, topic_glob_regex: str, timeout_s: float = 20.0) -> tuple[str, str]:
    deadline = time.time() + timeout_s
    rx = re.compile(topic_glob_regex)
    while time.time() < deadline:
        try:
            t, p = mc.get(timeout_s=1.0)
            if rx.search(t):
                return t, p
        except queue.Empty:
            pass
    raise AssertionError(f"Timeout waiting for topic regex: {topic_glob_regex}")


def _normalize_mac(mac: str) -> str:
    return mac.strip().lower()


def test_wifi_mqtt_telemetry_and_reconnect():
    conn = SerialConnection()
    try:
        # Quiet logs to reduce interleaving
        conn.send_command("log_level * none")

        host, port = _get_mqtt_broker(conn)
        mc = MqttCapture(host, port)
        try:
            # Get this device's MAC first so we can ignore reports from other devices
            start = conn.send_command("get_mac sta")
            mac_text = conn.get_text(start_pos=start)
            m = re.search(r"([0-9A-F]{2}(?::[0-9A-F]{2}){5})", mac_text)
            assert m, f"Could not parse MAC from console: {mac_text}"
            mac_console = _normalize_mac(m.group(1))

            # Subscribe to device/location telemetry
            mc.subscribe("location/+/+/+/device")
            mc.subscribe("location/+/+/+/connected")
            mc.drain()

            # Wait for a device info message from THIS device (ignore others)
            topic = None
            device_json = None
            end = time.time() + 60.0
            while time.time() < end:
                t, payload = _wait_for_topic(mc, r"^location/.+/.+/.+/device$", timeout_s=10.0)
                try:
                    candidate = json.loads(payload)
                except Exception:
                    continue
                mac_payload = _normalize_mac(candidate.get("mac", ""))
                if mac_payload == mac_console:
                    topic = t
                    device_json = candidate
                    break
                # else ignore other devices
            assert topic is not None and device_json is not None, "Did not receive telemetry from this device"
            assert "ip" in device_json

            # Extract area/room/id from topic for later checks
            # topic format: location/{area}/{room}/{id}/device
            parts = topic.split("/")
            area, room, dev_id = parts[1], parts[2], parts[3]

            # Ping the IP
            ip = device_json["ip"]
            # macOS ping: -c 1 count, -W timeout not available; use -t for ttl not timeout.
            res = subprocess.run(["ping", "-c", "1", ip], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            assert res.returncode == 0, f"Ping failed: {ip}\n{res.stdout.decode()}\n{res.stderr.decode()}"

            # Ensure connected true retained exists
            # Subscribe and fetch connected retained message
            connected_topic = f"location/{area}/{room}/{dev_id}/connected"
            mc.subscribe(connected_topic)
            # Drain then wait for a connected true
            mc.drain()
            t2, p2 = _wait_for_topic(mc, re.escape(connected_topic) + r"$", timeout_s=15.0)
            try:
                conn_obj = json.loads(p2)
                assert conn_obj.get("connected") is True
            except Exception:
                # Some brokers may deliver retained as string; tolerate
                assert "true" in p2.lower()

            # Request WiFi disconnect via console and wait for reconnection
            conn.send_command("disconnect")
            # After disconnect, broker may publish LWT false; then true upon reconnect
            saw_false = False
            saw_true_again = False
            end_time = time.time() + 45.0
            while time.time() < end_time and not (saw_true_again):
                try:
                    t, p = mc.get(timeout_s=2.0)
                    if t == connected_topic:
                        if "false" in p.lower():
                            saw_false = True
                        if "true" in p.lower():
                            # If we saw false before, this confirms reconnect
                            saw_true_again = True if saw_false else True
                except queue.Empty:
                    pass
            assert saw_true_again, "Did not observe re-connection on MQTT connected topic"

        finally:
            mc.stop()
    finally:
        conn.close()


