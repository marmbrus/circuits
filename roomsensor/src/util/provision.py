#!/usr/bin/env python3
import argparse
import base64
import contextlib
import json
import os
import pathlib
import re
import subprocess
import sys
import time
from typing import Optional

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("pyserial is required. Install with: pip install pyserial", file=sys.stderr)
    sys.exit(1)

# Add the util directory to the path to import from roomsensor_util
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from roomsensor_util.serial_console import SerialConsole


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


def _escape_str_value(value: str) -> str:
    # Basic escaping for quotes and backslashes
    return json.dumps(value)


def _infer_type_and_value(v):
    if isinstance(v, bool):
        return ("u8", "1" if v else "0")
    if isinstance(v, int):
        # NVS supports up to 64-bit integers, but let's default to i32 for broad compatibility.
        if -2147483648 <= v <= 2147483647:
            return ("i32", str(v))
        else:
            return ("i64", str(v))
    if isinstance(v, float):
        # NVS doesn't have a float type, store as a string.
        return ("str", str(v))
    # leave everything else as string
    return ("str", str(v))


def apply_config_file(console: SerialConsole, config_path: pathlib.Path):
    print(f"Applying configuration from {config_path.name}...")
    try:
        with open(config_path, 'r') as f:
            data = json.load(f)
    except FileNotFoundError:
        print(f"Warning: Config file not found: {config_path}", file=sys.stderr)
        return
    except json.JSONDecodeError as e:
        print(f"Error: Invalid JSON in {config_path}: {e}", file=sys.stderr)
        return

    if not isinstance(data, dict):
        print(f"Error: Top-level JSON in {config_path} must be an object.", file=sys.stderr)
        return

    for namespace, kvs in data.items():
        if not isinstance(kvs, dict):
            print(f"Warning: Skipping non-object value for namespace '{namespace}' in {config_path.name}", file=sys.stderr)
            continue

        print(f"  Setting namespace: {namespace}")
        mark_ns = console.get_clean_mark()
        console.write_line(f"nvs_namespace {namespace}")
        console.send_return()
        if not console.wait_for_clean_after(f"Namespace set to '{namespace}'", mark_ns, timeout_s=5):
            print(f"Error: Failed to set namespace '{namespace}'. Aborting.", file=sys.stderr)
            return

        for key, value in kvs.items():
            nvs_type, nvs_value = _infer_type_and_value(value)
            # For string values, the C-side command parser expects them to be unquoted on the command line,
            # but our helper function returns a JSON-dumped string (with quotes). We'll send the raw value.
            cmd_value = value if nvs_type == "str" else nvs_value

            print(f"    Setting {key} = {cmd_value}")
            mark_set = console.get_clean_mark()
            console.write_line(f"nvs_set {key} {nvs_type} -v {cmd_value}")
            console.send_return()
            if not console.wait_for_clean_after(f"Value stored under key '{key}'", mark_set, timeout_s=5):
                print(f"Error: Failed to set key '{key}' in namespace '{namespace}'.", file=sys.stderr)
                # Continue with other keys
    print(f"Finished applying {config_path.name}.")


# Base64 characters per `mqtt_tls add` line; a console line holds 255.
MQTT_TLS_CHUNK = 192


def run_mqtt_tls(console: SerialConsole, args: str, timeout_s: float = 10.0) -> str:
    """Runs `mqtt_tls <args>` on the board and returns its output, or raises if it fails."""
    mark = console.get_clean_mark()
    console.write_line(f"mqtt_tls {args}")
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        out = console.get_clean_text()[mark:]
        for answer in ("MQTT TLS ok", "MQTT TLS error"):
            i = out.find(answer)
            if i != -1 and "\n" in out[i:]:
                if answer == "MQTT TLS error":
                    # The board logs why under its mqtt_tls tag
                    why = [l.strip() for l in out.splitlines() if "mqtt_tls:" in l or "MQTT TLS error" in l]
                    raise RuntimeError("\n".join(why))
                return out
        time.sleep(0.02)
    raise RuntimeError(f"No answer to 'mqtt_tls {args.split()[0]}'; does the board's firmware have it?")


def provision_mqtt_cert(console: SerialConsole, authority, ca_pem: Optional[str]) -> None:
    """Mints the board's MQTT client certificate with the YubiKey and stores it on the board."""
    from roomsensor_util import mqtt_cert

    status = run_mqtt_tls(console, "status")
    match = re.search(r"MQTT TLS name: (ESP_[0-9a-f]{12})", status)
    if not match:
        raise RuntimeError("The board didn't say its certificate name")
    name = match.group(1)
    print(f"Minting MQTT certificate for {name} with YubiKey {authority.serial}...")
    cert = mqtt_cert.mint(name, authority.certificate, authority.sign, ca_pem=ca_pem)

    run_mqtt_tls(console, "begin")
    for part, pem in (("cert", cert.certificate_pem), ("key", cert.key_pem), ("ca", cert.ca_pem)):
        b64 = base64.b64encode(pem.encode()).decode()
        for i in range(0, len(b64), MQTT_TLS_CHUNK):
            run_mqtt_tls(console, f"add {part} {b64[i:i + MQTT_TLS_CHUNK]}")
    run_mqtt_tls(console, "commit")
    print(f"  Stored {mqtt_cert.describe(cert.certificate)}, serial {cert.certificate.serial_number:x}, "
          f"until {cert.certificate.not_valid_after_utc:%Y-%m-%d}")
    print(f"  The broker names it O={mqtt_cert.ORGANIZATION},CN={name}; give it a certificate account on "
          f"Brayden's Security tab, and set wifi.mqtt_broker to mqtts://<broker>:8883 to use it.")


def open_mqtt_authority(stack: contextlib.ExitStack, serial: Optional[int], ca_path: Optional[str]):
    """The YubiKey's authority, held open on `stack`, and the CA bundle for the board (None: the
    YubiKey's own CA certificate). Exits if either isn't usable."""
    try:
        from roomsensor_util import mqtt_cert
    except ImportError as e:
        print(f"--mqtt-cert needs yubikey-manager ({e}). Install with: pip install yubikey-manager", file=sys.stderr)
        sys.exit(1)
    try:
        authority = stack.enter_context(mqtt_cert.YubiKeyAuthority(serial))
    except Exception as e:  # noqa: BLE001
        print(f"Error: can't use the YubiKey: {e}", file=sys.stderr)
        sys.exit(1)
    print(f"MQTT certificates: YubiKey {authority.serial}, {mqtt_cert.describe(authority.certificate)}")

    ca_pem = None
    if ca_path:
        try:
            ca_pem = pathlib.Path(ca_path).read_text()
            bundle = mqtt_cert.authorities(ca_pem)
        except Exception as e:  # noqa: BLE001
            print(f"Error: can't read the authorities in {ca_path}: {e}", file=sys.stderr)
            sys.exit(1)
        if authority.certificate not in bundle:
            print(f"Warning: {ca_path} doesn't have YubiKey {authority.serial}'s CA certificate", file=sys.stderr)
    return authority, ca_pem


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Flash and provision the device with configuration from JSON files.",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument(
        "config_files",
        nargs="*",
        help="One or more paths to JSON configuration files to apply in order."
    )
    parser.add_argument("--port", help="Serial port to use (default: auto-detect)")
    parser.add_argument(
        "--skip-flash",
        action="store_true",
        help="Skip the 'erase-flash' and 'flash' steps."
    )
    parser.add_argument(
        "--program",
        action="store_true",
        help="Only program configuration (skip flashing, equivalent to --skip-flash)."
    )
    parser.add_argument(
        "--mqtt-cert",
        action="store_true",
        help="Give the board a new MQTT client certificate (CN=ESP_<mac>, O=acgt.net),\n"
             "signed by the CA on the YubiKey plugged into this computer."
    )
    parser.add_argument("--yubikey", type=int, help="Serial of the YubiKey to sign with, when more than one is plugged in")
    parser.add_argument(
        "--mqtt-ca",
        help="PEM file of the authorities the board checks the broker's certificate against\n"
             "(default: the YubiKey's CA certificate). On smll: ~/.brayden/config/security/mqtt/broker/ca.pem"
    )
    args = parser.parse_args()
    
    # --program implies --skip-flash
    if args.program:
        args.skip_flash = True

    # --- Start: Fail-fast validation ---
    for config_file in args.config_files:
        path = pathlib.Path(config_file)
        if not path.is_file():
            print(f"Error: Configuration file not found: {config_file}", file=sys.stderr)
            sys.exit(1)
        try:
            with open(path, 'r') as f:
                json.load(f)
        except json.JSONDecodeError as e:
            print(f"Error: Invalid JSON in {config_file}: {e}", file=sys.stderr)
            sys.exit(1)
        except Exception as e:
            print(f"Error: Could not read file {config_file}: {e}", file=sys.stderr)
            sys.exit(1)
    stack = contextlib.ExitStack()
    authority, mqtt_ca_pem = None, None
    if args.mqtt_cert:
        authority, mqtt_ca_pem = open_mqtt_authority(stack, args.yubikey, args.mqtt_ca)
    # --- End: Fail-fast validation ---

    # The script is in util/, so the project root is one level up.
    project_root = pathlib.Path(__file__).resolve().parent.parent

    if not args.skip_flash:
        print("Erasing and flashing device...")
        try:
            # Source the ESP-IDF environment script and run idf.py
            cmd = ". ~/esp/v5.3/esp-idf/export.sh && idf.py erase-flash flash"
            process = subprocess.Popen(
                ["bash", "-c", cmd],
                cwd=project_root,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True
            )
            for line in iter(process.stdout.readline, ''):
                print(line, end='')
            process.wait()
            if process.returncode != 0:
                print(f"\n\nError: idf.py command failed with exit code {process.returncode}.", file=sys.stderr)
                sys.exit(1)
            print("Flash complete.")
        except Exception as e:
            print(f"An error occurred during flashing: {e}", file=sys.stderr)
            sys.exit(1)
    else:
        print("Skipping flash as requested.")

    port = pick_port(args.port)
    console = SerialConsole(port=port)
    try:
        console.open()
        print("Resetting device...")
        console.reset_normal()
        print("Waiting for console to initialize...")
        if not console.wait_for("Console initialized", timeout_s=10):
            print("Error: Did not see 'Console initialized' message.", file=sys.stderr)
            console.dump_recent_output()
            sys.exit(1)
        if not console.wait_for("Startup sequence complete", timeout_s=10):
            print("Error: Did not see 'Startup sequence complete' message.", file=sys.stderr)
            console.dump_recent_output()
            sys.exit(1)

        print("Device initialized. Applying configuration...")

        # 1. Apply credentials.json (if it exists in the project root)
        credentials_path = project_root / 'credentials.json'
        if credentials_path.exists():
            apply_config_file(console, credentials_path)
        else:
            # This is not a fatal error, as credentials might not be needed.
            print("No 'credentials.json' found in project root, skipping.")

        # 2. Apply config files from command line arguments
        for config_file in args.config_files:
            apply_config_file(console, pathlib.Path(config_file))

        # 3. Give the board its MQTT client certificate
        if authority is not None:
            try:
                provision_mqtt_cert(console, authority, mqtt_ca_pem)
            except Exception as e:  # noqa: BLE001
                # No dump of the serial output here: it has the key in it
                print(f"Error: MQTT certificate not stored: {e}", file=sys.stderr)
                sys.exit(1)

        # 4. Restart the device to apply all settings
        print("Configuration applied. Restarting device...")
        console.write_line("restart")
        console.send_return()
        time.sleep(1) # Give it a moment to process the restart

    finally:
        console.close()
        stack.close()

    print("\nProvisioning complete.")


if __name__ == "__main__":
    main()
