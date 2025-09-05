#!/usr/bin/env python3
import argparse
import sys
import time
import re
import os
import json
import pathlib
from typing import Optional

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("pyserial is required. Install with: pip install pyserial", file=sys.stderr)
    sys.exit(1)

BAUD = 115200


def pick_port(user_port: Optional[str]) -> str:
    if user_port:
        return user_port
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial ports found", file=sys.stderr)
        sys.exit(2)

    def port_score(p) -> int:
        device_lower = (p.device or "").lower()
        desc_lower = (p.description or "").lower()
        score = 0
        if ("usbserial" in device_lower or "usbmodem" in device_lower
                or "wchusbserial" in device_lower or "slab_usbto" in device_lower
                or "usb" in device_lower):
            score += 100
        if "usb" in desc_lower:
            score += 50
        if "bluetooth" in device_lower or "bluetooth" in desc_lower:
            score -= 200
        try:
            if getattr(p, 'vid', None) is not None and getattr(p, 'pid', None) is not None:
                score += 10
        except Exception:
            pass
        return score

    ports_sorted = sorted(ports, key=port_score, reverse=True)
    print("Available ports (preferred first):")
    for i, p in enumerate(ports_sorted):
        print(f"  [{i}] {p.device} - {p.description}")
    best = ports_sorted[0]
    print(f"Using: {best.device}")
    return best.device


def open_console(port: str) -> serial.Serial:
    ser = serial.Serial(port=port, baudrate=BAUD, timeout=0.2)
    time.sleep(0.2)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    return ser


ANSI_OSC_RE = re.compile(r"\x1B\].*?(?:\x07|\x1B\\)")
ANSI_CSI_RE = re.compile(r"\x1B\[[0-?]*[ -/]*[@-~]")


def _sanitize_terminal_output(raw: bytes) -> str:
    try:
        text = raw.decode("utf-8", errors="replace")
    except Exception:
        text = str(bytes(raw))
    # Remove OSC (Operating System Command) and CSI (Control Sequence Introducer) sequences
    text = ANSI_OSC_RE.sub("", text)
    text = ANSI_CSI_RE.sub("", text)
    return text


def send_line_and_capture(ser: serial.Serial, line: str) -> str:
    try:
        ser.write((line + "\r\n").encode("utf-8"))
        ser.flush()
    except Exception:
        pass

    # Read until idle
    idle_threshold_s = 0.4
    max_wait_s = 6.0
    start = time.time()
    last_data = start
    buf = bytearray()
    while True:
        data = ser.read(256)
        now = time.time()
        if data:
            # Respond to cursor position report request (ESC[6n) from linenoise
            # to simulate a real terminal and avoid it swallowing our input.
            if b"\x1b[6n" in data:
                try:
                    ser.write(b"\x1b[1;1R")
                    ser.flush()
                except Exception:
                    pass
                # Remove the query from captured output
                data = data.replace(b"\x1b[6n", b"")
            buf.extend(data)
            last_data = now
        else:
            if (now - last_data) >= idle_threshold_s:
                break
            if (now - start) >= max_wait_s:
                break
            time.sleep(0.02)
    return _sanitize_terminal_output(buf)


def _escape_str_value(value: str) -> str:
    return value.replace('\\', r'\\').replace('"', r'\"')


def _infer_type_and_value(v):
    if isinstance(v, bool):
        return ("u8", "1" if v else "0")
    if isinstance(v, int):
        return ("i32", str(v))
    # leave everything else as string
    return ("str", f'"{_escape_str_value(str(v))}"')


def build_commands_from_dict(data: dict) -> list[str]:
    if not isinstance(data, dict):
        raise SystemExit("Top-level JSON must be an object mapping namespaces to key/value objects.")

    commands: list[str] = []
    # quiet logs first
    commands.append("log_level * none")
    for namespace, kvs in data.items():
        if not isinstance(kvs, dict):
            continue
        commands.append(f"nvs_namespace {namespace}")
        for key, value in kvs.items():
            t, val = _infer_type_and_value(value)
            commands.append(f"nvs_set {key} {t} -v {val}")
    # issue restart at the end
    commands.append("restart")
    return commands


def _merge_configs(base: dict, overlay: dict) -> dict:
    """Shallow-merge two config dicts by namespace; dict values are merged with overlay taking precedence."""
    out = dict(base)
    for ns, kv in (overlay or {}).items():
        if isinstance(kv, dict) and isinstance(out.get(ns), dict):
            merged = dict(out[ns])
            merged.update(kv)
            out[ns] = merged
        else:
            out[ns] = kv
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description="Send one or more lines to the device and print all output, or apply a JSON config file (merged with credentials.json if present).")
    parser.add_argument("cmd", nargs="*", help="Commands to send (quote each command) or a single JSON file path")
    parser.add_argument("--port", help="Serial port (default: first available)")
    args = parser.parse_args()

    commands: list[str]
    if len(args.cmd) == 1 and (args.cmd[0].endswith('.json') or os.path.isfile(args.cmd[0])):
        # Load specified JSON
        with open(args.cmd[0], 'r') as f:
            cfg = json.load(f)
        # Optionally load credentials.json from the same directory as this script
        script_dir = pathlib.Path(__file__).resolve().parent
        cred_path = script_dir / 'credentials.json'
        if cred_path.exists():
            try:
                with open(cred_path, 'r') as cf:
                    creds = json.load(cf)
                cfg = _merge_configs(cfg, creds)
            except Exception as e:
                print(f"Warning: failed to read credentials.json: {e}", file=sys.stderr)
        commands = build_commands_from_dict(cfg)
    else:
        # Raw command mode; if nothing provided, just print help-like behavior
        commands = args.cmd if args.cmd else [""]

    port = pick_port(args.port)
    ser = open_console(port)
    try:
        for idx, line in enumerate(commands):
            print("=== start ===")
            output = send_line_and_capture(ser, line)
            if output:
                print(output, end="")
            print("\n=== end ===")
            # brief pause between commands
            time.sleep(0.1)
    finally:
        ser.close()


if __name__ == "__main__":
    main()


