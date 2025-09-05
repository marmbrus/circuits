import re
import sys
import time
import threading
from typing import Optional, Tuple

"""
Stable practices captured from program.py and manual sessions:
 - Reset device via RTS/DTR on connect to a known good state
 - Mirror terminal behavior: respond to ESC[6n
 - Strip ANSI to keep assertions and shell clean
 - For each command: CRLF, CRLF, Ctrl-U+CRLF, write once, wait for echo
No multiple modes, no complex retries — keep it simple for robustness.
"""

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("pyserial is required. Install with: pip install pyserial", file=sys.stderr)
    raise


ANSI_OSC_RE = re.compile(r"\x1B\].*?(?:\x07|\x1B\\)")
ANSI_CSI_RE = re.compile(r"\x1B\[[0-?]*[ -/]*[@-~]")
PROMPT_TEXT = "esp32> "


def _sanitize(text: str) -> str:
    text = ANSI_OSC_RE.sub("", text)
    text = ANSI_CSI_RE.sub("", text)
    return text


def pick_port(preferred: Optional[str] = None) -> str:
    if preferred:
        return preferred
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        raise RuntimeError("No serial ports found")

    def score(p) -> int:
        device_lower = (p.device or "").lower()
        desc_lower = (p.description or "").lower()
        s = 0
        if ("usbserial" in device_lower or "usbmodem" in device_lower
                or "wchusbserial" in device_lower or "slab_usbto" in device_lower
                or "usb" in device_lower):
            s += 100
        if "usb" in desc_lower:
            s += 50
        if "bluetooth" in device_lower or "bluetooth" in desc_lower:
            s -= 200
        try:
            if getattr(p, 'vid', None) is not None and getattr(p, 'pid', None) is not None:
                s += 10
        except Exception:
            pass
        return s

    ports_sorted = sorted(ports, key=score, reverse=True)
    best = ports_sorted[0]
    print("Available ports (preferred first):")
    for i, p in enumerate(ports_sorted):
        print(f"  [{i}] {p.device} - {p.description}")
    print(f"Using: {best.device}")
    return best.device


class SerialConnection:
    def __init__(self, port: Optional[str] = None, baud: int = 115200, buffer_limit_bytes: int = 1_000_000) -> None:
        self.port = pick_port(port)
        self.baud = baud
        self._ser = serial.Serial(port=self.port, baudrate=self.baud, timeout=0.1)
        time.sleep(0.1)
        self._ser.reset_input_buffer()
        self._ser.reset_output_buffer()
        # Circular buffer state
        self._buf = bytearray()
        self._buf_base_index = 0  # logical index of first byte in _buf
        self._buf_limit = buffer_limit_bytes
        self._lock = threading.Lock()
        self._stop = False
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()
        # Immediately reset to a known-good run state so console is clean
        try:
            self.reset_run()
            self.wait_for_boot()
            self.wait_for_idle()
        except Exception:
            pass

    def close(self) -> None:
        self._stop = True
        try:
            self._reader.join(timeout=0.5)
        except Exception:
            pass
        try:
            self._ser.close()
        except Exception:
            pass

    # Internal read loop
    def _read_loop(self) -> None:
        while not self._stop:
            try:
                data = self._ser.read(4096)
                if not data:
                    continue
                # Reply to cursor position query (ESC[6n) to emulate terminal
                if b"\x1b[6n" in data:
                    try:
                        self._ser.write(b"\x1b[1;1R")
                        self._ser.flush()
                    except Exception:
                        pass
                    data = data.replace(b"\x1b[6n", b"")
                with self._lock:
                    self._buf.extend(data)
                    # Enforce buffer limit
                    if len(self._buf) > self._buf_limit:
                        drop = len(self._buf) - self._buf_limit
                        del self._buf[:drop]
                        self._buf_base_index += drop
            except serial.SerialException:
                break
            except Exception:
                time.sleep(0.05)

    def _snapshot_text(self) -> str:
        with self._lock:
            raw = bytes(self._buf)
        return _sanitize(raw.decode("utf-8", errors="replace"))

    def _wait_echo(self, line: str, *, timeout_s: float = 2.0) -> bool:
        deadline = time.time() + timeout_s
        needle = f"{line}\n"
        while time.time() < deadline:
            if needle in self._snapshot_text():
                return True
            time.sleep(0.05)
        return False

    def wait_for_boot(self, timeout_s: float = 10.0) -> None:
        self.assert_contains("boot: Multicore bootloader", timeout_s=timeout_s)

    def wait_for_idle(self, silence_s: float = 1.0, timeout_s: float = 20.0) -> None:
        deadline = time.time() + timeout_s
        last_data_time = time.time()
        while time.time() < deadline:
            current_len = len(self._buf)
            time.sleep(0.1)
            if len(self._buf) > current_len:
                last_data_time = time.time()
            if time.time() - last_data_time > silence_s:
                return
        raise RuntimeError("Console did not become idle")

    def reset_run(self, *, wait_after_s: float = 0.3) -> int:
        """Hardware reset to normal run mode using RTS/DTR. Returns buffer mark prior to reset."""
        with self._lock:
            start_pos = self._buf_base_index + len(self._buf)
        try:
            # Ensure boot mode is normal (GPIO0 high => DTR False), then pulse EN (RTS True -> False)
            self._ser.setDTR(False)
            time.sleep(0.02)
            self._ser.setRTS(True)
            time.sleep(0.1)
            self._ser.setRTS(False)
        except Exception:
            pass
        time.sleep(wait_after_s)
        return start_pos

    def send_command(self, line: str) -> int:
        """Send a single command line robustly."""
        with self._lock:
            start_pos = self._buf_base_index + len(self._buf)
        
        # Atomic single write of the command + CRLF
        try:
            self._ser.write((line + "\r\n").encode("utf-8"))
            self._ser.flush()
        except Exception:
            pass
        
        self._wait_echo(line)
        return start_pos

    def _snapshot_from(self, start_pos: Optional[int]) -> str:
        with self._lock:
            rel = 0 if start_pos is None else max(0, start_pos - self._buf_base_index)
            raw = bytes(self._buf[rel:])
        text = raw.decode("utf-8", errors="replace")
        return _sanitize(text)

    def assert_contains(self, needle: str, *, start_pos: Optional[int] = None, timeout_s: float = 5.0, poll_s: float = 0.05) -> None:
        deadline = time.time() + timeout_s
        while True:
            hay = self._snapshot_from(start_pos)
            if needle in hay:
                return
            if time.time() >= deadline:
                raise AssertionError(
                    "\n".join([
                        f"Did not find substring after {timeout_s}s: {needle!r}",
                        "--- captured output (full) ---",
                        hay,
                        "--- end captured output ---",
                    ])
                )
            time.sleep(poll_s)

    def assert_matches(self, pattern: str, *, start_pos: Optional[int] = None, timeout_s: float = 5.0, flags: int = 0, poll_s: float = 0.05) -> None:
        rx = re.compile(pattern, flags)
        deadline = time.time() + timeout_s
        while True:
            hay = self._snapshot_from(start_pos)
            if rx.search(hay) is not None:
                return
            if time.time() >= deadline:
                raise AssertionError(
                    "\n".join([
                        f"Did not match regex after {timeout_s}s: {pattern!r}",
                        "--- captured output (full) ---",
                        hay,
                        "--- end captured output ---",
                    ])
                )
            time.sleep(poll_s)

    def wait_match(self, pattern: str, *, start_pos: Optional[int] = None, timeout_s: float = 5.0, flags: int = 0, poll_s: float = 0.05) -> re.Match:
        rx = re.compile(pattern, flags)
        deadline = time.time() + timeout_s
        while True:
            hay = self._snapshot_from(start_pos)
            m = rx.search(hay)
            if m is not None:
                return m
            if time.time() >= deadline:
                raise AssertionError(
                    "\n".join([
                        f"Did not match regex after {timeout_s}s: {pattern!r}",
                        "--- captured output (full) ---",
                        hay,
                        "--- end captured output ---",
                    ])
                )
            time.sleep(poll_s)

    def get_text(self, *, start_pos: Optional[int] = None) -> str:
        return self._snapshot_from(start_pos)


