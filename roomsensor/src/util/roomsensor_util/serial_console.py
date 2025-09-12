import os
import time
import threading
import logging
import re
from typing import Optional

import serial
from serial.tools import list_ports


logger = logging.getLogger(__name__)


# Matches ANSI escape codes (CSI and other common sequences)
ANSI_ESCAPE_RE = re.compile(r"\x1B\[[0-?]*[ -/]*[@-~]")


def _clean_ansi(text: str) -> str:
    return ANSI_ESCAPE_RE.sub("", text)


def find_default_port() -> Optional[str]:
    """Find the first serial port that looks like a usbserial device.

    Returns the device path or None if not found.
    """
    env_port = os.environ.get("ROOMSENSOR_SERIAL_PORT")
    if env_port:
        logger.info("Using ROOMSENSOR_SERIAL_PORT=%s", env_port)
        return env_port

    ports = list(list_ports.comports())
    for p in ports:
        name = (p.device or "") + " " + (p.description or "")
        if "usbserial" in name.lower():
            logger.info("Auto-selected port: %s (%s)", p.device, p.description)
            return p.device
    if ports:
        logger.warning(
            "No port matched 'usbserial'; falling back to first available: %s (%s)",
            ports[0].device,
            ports[0].description,
        )
        return ports[0].device
    logger.error("No serial ports found")
    return None


class SerialConsole:
    """Simple serial connection to the RoomSensor console.

    - Auto-selects serial port if not provided
    - Performs hardware reset using RTS/DTR on connect
    - Captures all output into an internal buffer
    - Provides utilities to wait for output and query buffer length
    """

    def __init__(
        self,
        port: Optional[str] = None,
        baudrate: int = 115200,
        timeout: float = 0.05,
    ) -> None:
        self.port = port or find_default_port()
        if not self.port:
            raise RuntimeError("No serial port available")
        self.baudrate = baudrate
        self.timeout = timeout
        self._ser: Optional[serial.Serial] = None
        self._reader_thread: Optional[threading.Thread] = None
        self._stop_event = threading.Event()
        self._buffer_lock = threading.Lock()
        self._buffer = bytearray()
        self._clean_chunks: list[str] = []

    # Context manager support
    def __enter__(self) -> "SerialConsole":
        self.open()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    # Lifecycle
    def open(self) -> None:
        logger.info("Opening serial port %s @ %d", self.port, self.baudrate)
        # Disable HW flow control so manual DTR/RTS toggling works as expected
        self._ser = serial.Serial(
            self.port,
            self.baudrate,
            timeout=self.timeout,
            rtscts=False,
            dsrdtr=False,
        )
        # Ensure DTR and RTS are de-asserted (logic high) initially
        self._set_dtr(False)
        self._set_rts(False)

        self._is_running = True
        self._reader_thread = threading.Thread(target=self._reader_loop, daemon=True)
        self._reader_thread.start()

    def close(self) -> None:
        logger.info("Closing serial port")
        self._stop_event.set()
        if self._reader_thread and self._reader_thread.is_alive():
            self._reader_thread.join(timeout=1.0)
        if self._ser and self._ser.is_open:
            # Deassert control lines before closing so ESP32 is left in run mode
            try:
                try:
                    self._ser.break_condition = False
                except Exception:  # noqa: BLE001
                    pass
                try:
                    self._ser.setDTR(False)
                except Exception:  # noqa: BLE001
                    pass
                try:
                    self._ser.setRTS(False)
                except Exception:  # noqa: BLE001
                    pass
                time.sleep(0.02)
            except Exception:  # noqa: BLE001
                logger.exception("Error while deasserting control lines")
            try:
                self._ser.close()
            except Exception:  # noqa: BLE001
                logger.exception("Error while closing serial port")
        self._ser = None
        self._reader_thread = None
        self._stop_event.clear()

    # Reset helpers (ESP32 auto-reset: RTS->EN, DTR->IO0; True asserts = logic LOW)
    def _set_dtr(self, asserted: bool) -> None:
        assert self._ser is not None
        self._ser.setDTR(asserted)
        logger.info("DTR asserted=%s", asserted)

    def _set_rts(self, asserted: bool) -> None:
        assert self._ser is not None
        self._ser.setRTS(asserted)
        logger.info("RTS asserted=%s", asserted)

    def reset_bootloader(self, pulse_s: float = 0.05) -> None:
        """Enter bootloader: hold IO0 low while pulsing EN low->high."""
        assert self._ser is not None
        logger.info("Reset sequence: bootloader")
        self._set_dtr(True)   # IO0 low
        time.sleep(0.01)
        self._set_rts(True)   # EN low (reset)
        time.sleep(pulse_s)
        self._set_rts(False)  # EN high (release reset)
        time.sleep(0.05)
        self._set_dtr(False)  # IO0 high

    def reset_normal(self, pulse_s: float = 0.05) -> None:
        """Normal run: IO0 high, pulse EN low->high."""
        assert self._ser is not None
        logger.info("Reset sequence: normal run")
        self._set_dtr(False)  # IO0 high
        time.sleep(0.01)
        self._set_rts(True)   # EN low (reset)
        time.sleep(pulse_s)
        self._set_rts(False)  # EN high (release)
        time.sleep(0.05)

    # I/O
    def write(self, data: bytes) -> None:
        if not self._ser:
            raise RuntimeError("Serial not open")
        try:
            readable = _clean_ansi(data.decode("utf-8", errors="replace"))
        except Exception:  # noqa: BLE001
            readable = repr(data)
        logger.info("TX %s", readable)
        self._ser.write(data)
        self._ser.flush()

    def write_line(self, line: str) -> None:
        if self._ser is None:
            raise RuntimeError("Serial port is not open")
        
        self.write(line.encode("utf-8"))
        self.send_return()

    def send_return(self) -> None:
        self.write(b"\r")

    # Buffer utilities
    def get_buffer(self) -> bytes:
        with self._buffer_lock:
            return bytes(self._buffer)

    def get_buffer_length(self) -> int:
        with self._buffer_lock:
            return len(self._buffer)

    def get_mark(self) -> int:
        return self.get_buffer_length()

    def wait_for(self, text: str, timeout_s: float = 5.0) -> bool:
        """Wait until text appears in buffer within timeout."""
        deadline = time.time() + timeout_s
        target = text.encode("utf-8")
        while time.time() < deadline:
            buf = self.get_buffer()
            if target in buf:
                return True
            time.sleep(0.02)
        return False

    def wait_for_after(self, text: str, start: int, timeout_s: float = 5.0) -> bool:
        """Wait until text appears in buffer after start position within timeout."""
        deadline = time.time() + timeout_s
        target = text.encode("utf-8")
        while time.time() < deadline:
            buf = self.get_buffer()
            if buf.find(target, start) != -1:
                return True
            time.sleep(0.02)
        return False

    def dump_recent(self, max_bytes: int = 2048) -> str:
        buf = self.get_buffer()
        tail = buf[-max_bytes:]
        try:
            text = tail.decode("utf-8", errors="replace")
        except Exception:  # noqa: BLE001
            text = str(tail)
        logger.info("Recent output:\n%s", text)
        return text

    # Cleaned-text buffer utilities (ANSI removed)
    def get_clean_text(self) -> str:
        with self._buffer_lock:
            return "".join(self._clean_chunks)

    def get_clean_mark(self) -> int:
        return len(self.get_clean_text())

    def wait_for_clean_after(self, text: str, start: int, timeout_s: float = 5.0) -> bool:
        deadline = time.time() + timeout_s
        while time.time() < deadline:
            clean = self.get_clean_text()
            if clean.find(text, start) != -1:
                return True
            time.sleep(0.02)
        return False

    def dump_recent_output(self, num_lines: int = 50) -> None:
        """Prints the last `num_lines` of raw and clean output to the console."""
        print("\n--- DUMPING RECENT SERIAL OUTPUT ---")
        
        with self._buffer_lock:
            raw_text = self._buffer.decode(encoding="utf-8", errors="replace")
        
        raw_lines = raw_text.splitlines()
        print(f"\n--- Last {num_lines} lines of RAW output ---")
        for line in raw_lines[-num_lines:]:
            print(line)

        clean_text = self.get_clean_text()
        clean_lines = clean_text.splitlines()
        print(f"\n--- Last {num_lines} lines of CLEAN output ---")
        for line in clean_lines[-num_lines:]:
            print(line)
        
        print("\n--- END OF DUMP ---")

    # Internals
    def _start_reader(self) -> None:
        assert self._ser is not None
        self._stop_event.clear()
        t = threading.Thread(target=self._reader_loop, name="serial-reader", daemon=True)
        t.start()
        self._reader_thread = t

    def _reader_loop(self) -> None:
        assert self._ser is not None
        while not self._stop_event.is_set():
            try:
                data = self._ser.read(512)
                if data:
                    with self._buffer_lock:
                        self._buffer.extend(data)
                    # Log for debugging; can be reduced later
                    try:
                        decoded = data.decode("utf-8", errors="replace")
                        text = _clean_ansi(decoded)
                        if text:
                            with self._buffer_lock:
                                self._clean_chunks.append(text)
                            logger.info("RX %s", text.rstrip("\n"))
                    except Exception:  # noqa: BLE001
                        logger.info("RX %r", data)
                else:
                    time.sleep(0.01)
            except serial.SerialException:
                logger.exception("Serial read error")
                break
            except Exception:  # noqa: BLE001
                logger.exception("Unexpected error in reader loop")
                break


