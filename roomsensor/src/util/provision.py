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


# Base64 characters per `device_cert add` line; a console line holds 255.
CERT_CHUNK = 192


def run_device_cert(console: SerialConsole, args: str, timeout_s: float = 10.0) -> str:
    """Runs `device_cert <args>` on the board and returns its output, or raises if it fails."""
    mark = console.get_clean_mark()
    console.write_line(f"device_cert {args}")
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        out = console.get_clean_text()[mark:]
        for answer in ("DEVICE CERT ok", "DEVICE CERT error"):
            i = out.find(answer)
            if i != -1 and "\n" in out[i:]:
                if answer == "DEVICE CERT error":
                    # The board logs why under its device_cert tag
                    why = [l.strip() for l in out.splitlines() if "device_cert:" in l or "DEVICE CERT error" in l]
                    raise RuntimeError("\n".join(why))
                return out
        time.sleep(0.02)
    raise RuntimeError(f"No answer to 'device_cert {args.split()[0]}'; does the board's firmware have it?")


# How many times the certificate is sent before giving up.
CERT_ATTEMPTS = 3


def send_device_cert(console: SerialConsole, parts) -> None:
    """Sends the PEMs in `parts` ((name, pem), ...) to the board and commits them. After each piece,
    the board says how many characters it holds for that part, which must be what was sent: a
    stray byte on its serial input would otherwise corrupt the key unnoticed."""
    run_device_cert(console, "begin")
    for part, pem in parts:
        b64 = base64.b64encode(pem.encode()).decode()
        for i in range(0, len(b64), CERT_CHUNK):
            out = run_device_cert(console, f"add {part} {b64[i:i + CERT_CHUNK]}")
            held = re.search(rf"DEVICE CERT ok: {part} has (\d+) characters", out)
            expected = min(i + CERT_CHUNK, len(b64))
            if not held or int(held.group(1)) != expected:
                raise RuntimeError(f"the board holds {held.group(1) if held else 'an unknown number of'} characters of {part}, not {expected}")
    run_device_cert(console, "commit")


def board_certificate(console: SerialConsole):
    """The board's name (ESP_<mac>) and the subject of the certificate it holds, or None when it has none."""
    status = run_device_cert(console, "status")
    match = re.search(r"DEVICE CERT name: (ESP_[0-9a-f]{12})", status)
    if not match:
        raise RuntimeError("The board didn't say its certificate name")
    held = re.search(r"DEVICE CERT certificate: (.*)", status)
    subject = held.group(1).strip() if held else None
    return match.group(1), (None if subject in (None, "none") else subject)


def provision_device_cert(console: SerialConsole, authority, ca_pem: Optional[str], replace: bool = False) -> None:
    """Gets the board's certificate, from Brayden or the YubiKey plugged in here, and stores it on the board.
    Refuses to replace one the board already holds unless `replace`."""
    from roomsensor_util import device_cert

    name, held = board_certificate(console)
    if held and not replace:
        raise RuntimeError(f"{name} already has a certificate ({held}); pass --replace-cert to replace it")
    if isinstance(authority, device_cert.BraydenAuthority):
        cert = authority.mint(name, ca_pem=ca_pem)
    else:
        print(f"Minting a certificate for {name} with YubiKey {authority.serial}...")
        cert = device_cert.mint(name, authority.certificate, authority.sign, ca_pem=ca_pem)

    parts = (("cert", cert.certificate_pem), ("key", cert.key_pem), ("ca", cert.ca_pem))
    for attempt in range(1, CERT_ATTEMPTS + 1):
        try:
            send_device_cert(console, parts)
            break
        except RuntimeError as e:
            # A stray byte on the board's serial input corrupts a piece; the same key and
            # certificate can simply be sent again
            if attempt == CERT_ATTEMPTS:
                raise
            print(f"  The board didn't take it ({str(e).splitlines()[-1]}); sending it again ({attempt + 1}/{CERT_ATTEMPTS})...")
    print(f"  Stored {device_cert.describe(cert.certificate)}, serial {cert.certificate.serial_number:x}, "
          f"until {cert.certificate.not_valid_after_utc:%Y-%m-%d}")
    print(f"  It's used for OTA from updates.acgt.net, and for MQTT once wifi.mqtt_broker is "
          f"mqtts://mqtt.acgt.net:8883. Brayden names it O={device_cert.ORGANIZATION},CN={name}.")


def open_authority(stack: contextlib.ExitStack, yubikey: Optional[int], brayden_url: str, ca_path: Optional[str]):
    """Who signs the board's certificate, and the CA bundle for the board (None: what the signer
    trusts). Brayden, unless `yubikey` asks for the YubiKey plugged in here (0: the only one), which
    is held open on `stack`. Exits if either isn't usable."""
    try:
        from roomsensor_util import device_cert
    except ImportError as e:
        print(f"--device-cert needs the cryptography package ({e}). Install with: pip install -e util", file=sys.stderr)
        sys.exit(1)
    if yubikey is None:
        authority = device_cert.BraydenAuthority(brayden_url)
        print(f"Board certificates: from {authority.url}, approved on its dashboard")
    else:
        try:
            authority = stack.enter_context(device_cert.YubiKeyAuthority(yubikey or None))
        except ImportError as e:
            print(f"--yubikey needs yubikey-manager ({e}). Install with: pip install yubikey-manager", file=sys.stderr)
            sys.exit(1)
        except Exception as e:  # noqa: BLE001
            print(f"Error: can't use the YubiKey: {e}", file=sys.stderr)
            sys.exit(1)
        print(f"Board certificates: YubiKey {authority.serial}, {device_cert.describe(authority.certificate)}")

    ca_pem = None
    if ca_path:
        try:
            ca_pem = pathlib.Path(ca_path).read_text()
            bundle = device_cert.authorities(ca_pem)
        except Exception as e:  # noqa: BLE001
            print(f"Error: can't read the authorities in {ca_path}: {e}", file=sys.stderr)
            sys.exit(1)
        if isinstance(authority, device_cert.YubiKeyAuthority) and authority.certificate not in bundle:
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
        help="Skip the 'flash' step."
    )
    parser.add_argument(
        "--erase",
        action="store_true",
        help="Erase the whole flash before flashing: every setting and the board's certificate,\n"
             "which can't be recovered. Needs --delete-cert too. Without it, flashing keeps them."
    )
    parser.add_argument("--delete-cert", action="store_true",
                        help="Confirms that --erase may delete the board's certificate")
    parser.add_argument("--replace-cert", action="store_true",
                        help="Lets --device-cert replace a certificate the board already has")
    parser.add_argument(
        "--program",
        action="store_true",
        help="Only program configuration (skip flashing, equivalent to --skip-flash)."
    )
    parser.add_argument(
        "--device-cert",
        action="store_true",
        help="Give the board a new certificate (CN=ESP_<mac>, O=acgt.net) for MQTT and OTA.\n"
             "Brayden signs it once someone approves the request on its dashboard, with a YubiKey\n"
             "plugged into smll; or the YubiKey plugged in here, with --yubikey."
    )
    parser.add_argument(
        "--yubikey", type=int, nargs="?", const=0, metavar="SERIAL",
        help="Sign with the YubiKey plugged into this computer instead of through Brayden\n"
             "(its serial, when more than one is plugged in)"
    )
    parser.add_argument("--brayden", default="https://brayden.acgt.net", metavar="URL",
                        help="Brayden, which signs boards' certificates (default: %(default)s)")
    parser.add_argument(
        "--mqtt-ca",
        help="PEM file of the authorities the board checks the broker's certificate against\n"
             "(default: the house's authorities from Brayden, or the YubiKey's CA certificate)"
    )
    args = parser.parse_args()
    
    # --program implies --skip-flash
    if args.program:
        args.skip_flash = True
    # A board's certificate is only ever deleted when asked for twice over
    if args.erase and args.skip_flash:
        print("Error: --erase erases before flashing, so it can't go with --skip-flash or --program.", file=sys.stderr)
        sys.exit(1)
    if args.erase and not args.delete_cert:
        print("Error: --erase deletes the board's certificate along with its settings, and it can't be recovered;\n"
              "a new one needs approving again. Pass --delete-cert as well to go ahead.", file=sys.stderr)
        sys.exit(1)
    if args.replace_cert and not args.device_cert:
        print("Error: --replace-cert only goes with --device-cert.", file=sys.stderr)
        sys.exit(1)

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
    if args.device_cert:
        authority, mqtt_ca_pem = open_authority(stack, args.yubikey, args.brayden, args.mqtt_ca)
    # --- End: Fail-fast validation ---

    # The script is in util/, so the project root is one level up.
    project_root = pathlib.Path(__file__).resolve().parent.parent

    if not args.skip_flash:
        # Flashing alone leaves NVS, with the settings and the certificate, as it is
        print("Erasing and flashing device (settings and certificate deleted)..." if args.erase else "Flashing device...")
        try:
            # Source the ESP-IDF environment script and run idf.py
            cmd = ". ~/esp/v5.3/esp-idf/export.sh && idf.py " + ("erase-flash flash" if args.erase else "flash")
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
        # Startup waits up to 70 s for its boot message to reach the MQTT broker, which a board
        # whose broker or certificate isn't right yet never manages
        if not console.wait_for("Startup sequence complete", timeout_s=90):
            print("Error: Did not see 'Startup sequence complete' message.", file=sys.stderr)
            console.dump_recent_output()
            sys.exit(1)

        print("Device initialized.")
        # Before changing anything: a certificate already there stays unless --replace-cert
        if authority is not None and not args.replace_cert:
            try:
                name, held = board_certificate(console)
            except RuntimeError as e:
                print(f"Error: {e}", file=sys.stderr)
                sys.exit(1)
            if held:
                print(f"Error: {name} already has a certificate ({held}). Pass --replace-cert to replace it,\n"
                      "or leave out --device-cert to keep it.", file=sys.stderr)
                sys.exit(1)
        print("Applying configuration...")

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

        # 3. Give the board its certificate
        if authority is not None:
            try:
                provision_device_cert(console, authority, mqtt_ca_pem, replace=args.replace_cert)
            except Exception as e:  # noqa: BLE001
                # No dump of the serial output here: it has the key in it
                print(f"Error: certificate not stored: {e}", file=sys.stderr)
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
