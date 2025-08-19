import re
import time
from .serial_connection import SerialConnection


def test_reboot_and_boot_banner():
    conn = SerialConnection()
    try:
        # Hardware reset to ensure clean console state
        start = conn.reset_run()
        # Expect bootloader banner
        conn.assert_matches(r"I \(.*\) boot: Multicore bootloader", start_pos=start, timeout_s=10.0)
    finally:
        conn.close()


