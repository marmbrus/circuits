#!/usr/bin/env python3
"""Listens to a board's microphone over MQTT.

Boards stream their mic on sensor/<mac>/audio/mic while their audio.mic config is on
(IMA-ADPCM, 14.7 kHz, one message per 100 ms; roomsensor_util/mic.py decodes it).

    mic_listen.py 9c139ebc0eb0 --seconds 10 --out room.wav
    mic_listen.py 9c139ebc0eb0 --raw | ffplay -f s16le -ar 14700 -ch_layout mono -nodisp -
    mic_listen.py 9c139ebc0eb0 --enable ...   # turn the board's mic on now, and off at exit

Needs paho-mqtt (pip install -e '.[mic]').
"""

from __future__ import annotations

import argparse
import signal
import sys
import threading
import time
import wave

import paho.mqtt.client as mqtt

from roomsensor_util.mic import decode_block


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mac", help="the board's MAC, as in its topics (e.g. 9c139ebc0eb0)")
    parser.add_argument("--broker", default="mqtt.acgt.net")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--seconds", type=float, help="stop after this long (default: until Ctrl-C)")
    out = parser.add_mutually_exclusive_group(required=True)
    out.add_argument("--out", help="write a WAV file")
    out.add_argument("--raw", action="store_true", help="write 16-bit mono PCM to stdout")
    parser.add_argument("--enable", action="store_true",
                        help="set the board's audio/mic on while listening, and off at exit")
    args = parser.parse_args()

    mac = args.mac.lower().replace(":", "")
    topic = f"sensor/{mac}/audio/mic"
    config_topic = f"sensor/{mac}/config/audio/mic"
    done = threading.Event()
    state = {"rate": None, "next_seq": None, "blocks": 0, "missing": 0, "frames": bytearray()}

    def on_connect(client, _userdata, _flags, reason, _props=None):
        if reason.is_failure:
            print(f"connect failed: {reason}", file=sys.stderr)
            done.set()
            return
        client.subscribe(topic, qos=0)
        if args.enable:
            client.publish(config_topic, "true", qos=1)
        print(f"listening on {topic}", file=sys.stderr)

    def on_message(_client, _userdata, msg):
        try:
            block = decode_block(msg.payload)
        except ValueError as e:
            print(f"skipping a bad block: {e}", file=sys.stderr)
            return
        if state["next_seq"] is not None and block.sequence != state["next_seq"]:
            gap = (block.sequence - state["next_seq"]) & 0xFFFFFFFF
            if gap < 1 << 31:  # ahead: blocks were dropped (behind means the board restarted)
                state["missing"] += gap
                silence = bytes(2 * len(block.samples) * min(gap, 50))
                write(silence)
        state["next_seq"] = (block.sequence + 1) & 0xFFFFFFFF
        state["rate"] = block.sample_rate
        state["blocks"] += 1
        write(b"".join(s.to_bytes(2, "little", signed=True) for s in block.samples))

    def write(pcm: bytes):
        if args.raw:
            sys.stdout.buffer.write(pcm)
            sys.stdout.buffer.flush()
        else:
            state["frames"] += pcm

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    client.on_connect = on_connect
    client.on_message = on_message
    signal.signal(signal.SIGINT, lambda *_: done.set())
    client.connect(args.broker, args.port, keepalive=30)
    client.loop_start()
    started = time.monotonic()
    while not done.is_set():
        if args.seconds is not None and time.monotonic() - started >= args.seconds:
            break
        done.wait(0.2)
    if args.enable:
        client.publish(config_topic, "false", qos=1).wait_for_publish(5)
    client.loop_stop()
    client.disconnect()

    if args.out:
        with wave.open(args.out, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(state["rate"] or 14700)
            w.writeframes(bytes(state["frames"]))
    received = state["blocks"]
    print(f"{received} blocks ({received / 10:.1f} s of audio), {state['missing']} missing",
          file=sys.stderr)
    return 0 if received else 1


if __name__ == "__main__":
    sys.exit(main())
