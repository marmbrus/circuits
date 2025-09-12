import json
import os
import re
import subprocess
import time
from pathlib import Path

import pytest

from roomsensor_util.serial_console import SerialConsole


def _run_deploy_ota(root: Path, channel: str = "dev", timeout_s: int = 1800) -> subprocess.CompletedProcess:
    env = os.environ.copy()
    # Ensure non-interactive shell sources ESP-IDF and runs deploy script
    cmd = f". ~/esp/v5.3/esp-idf/export.sh; ./deploy_ota.sh {channel}"
    return subprocess.run(
        ["bash", "-lc", cmd],
        cwd=str(root),
        check=True,
        capture_output=True,
        text=True,
        timeout=timeout_s,
    )


def test_ota_channel_dev_and_status(console: SerialConsole):
    """
    Integration test for OTA subsystem:
    - Set release channel to "dev" in NVS (namespace: wifi, key: channel)
    - Reboot device to load config
    - Run deploy_ota.sh to publish a dev manifest
    - Observe serial logs for OTA status JSON and channel-specific manifest usage
    """

    # Determine repo root (src/) from this test location
    repo_root = Path(__file__).resolve().parents[2]

    # Ensure console responds
    mark = console.get_clean_mark()
    console.send_return()
    assert console.wait_for_clean_after("esp32>", mark, 5), "console prompt not detected"

    # Set NVS namespace to wifi and channel to dev
    mark = console.get_clean_mark()
    console.write_line("nvs_namespace wifi")
    assert console.wait_for_clean_after("Namespace set to 'wifi'", mark, 5)

    mark = console.get_clean_mark()
    console.write_line("nvs_set channel str -v dev")
    assert console.wait_for_clean_after("Value stored under key 'channel'", mark, 5)

    # Run OTA deploy for dev channel (builds firmware and web, uploads manifest)
    deploy = _run_deploy_ota(repo_root, channel="dev")
    assert deploy.returncode == 0, f"deploy_ota.sh failed: {deploy.stderr}"

    # Mark output before deployment so we can search deterministically
    boot_mark = console.get_clean_mark()

    # Reboot device so ConfigurationManager reloads NVS
    console.reset_normal()
    assert console.wait_for("Console initialized", timeout_s=15), "device did not reinitialize after reset"
    assert console.wait_for("Startup sequence complete", timeout_s=45), "device did not complete startup after reset"


    # Expect the device to use the dev manifest URL soon after
    assert console.wait_for_clean_after(
        "Using channel manifest: https://updates.gaia.bio/manifest-dev.json",
        boot_mark,
        180,
    ), "device did not switch to dev channel manifest"

    # The OTA loop should start a check shortly after SNTP sync
    assert console.wait_for_clean_after("Checking for updates", boot_mark, 180), "OTA did not start update check"
    pos_start = console.get_clean_mark()

    # Helper: wait for next OTA status in order
    status_re = re.compile(r"Publishing OTA status:\s*(\{.*?\})", re.DOTALL)

    def wait_for_status(target: str, start: int, timeout_s: int) -> int:
        deadline = time.time() + timeout_s
        search_from = start
        while time.time() < deadline:
            clean = console.get_clean_text()
            for m in status_re.finditer(clean, search_from):
                try:
                    payload = json.loads(m.group(1))
                except Exception:
                    search_from = m.end()
                    continue
                # Enforce channel and target status
                if payload.get("channel") != "dev":
                    search_from = m.end()
                    continue
                if payload.get("status") == target:
                    return m.end()
                # keep scanning for next match
                search_from = m.end()
            time.sleep(1.0)
        raise AssertionError(f"Timed out waiting for OTA status '{target}'")

    # Enforce ordered sequence with timeouts tied to the forced cycle mark.
    # 1) Firmware upgrade should start within 3 minutes of deploy/reboot
    pos = wait_for_status("UPGRADING_FIRMWARE", pos_start, timeout_s=180)

    # The device will reboot after firmware download. Wait for reboot to complete.
    assert console.wait_for("Console initialized", timeout_s=60), "device did not reboot back to console"
    assert console.wait_for("Startup sequence complete", timeout_s=90), "startup did not complete after OTA reboot"
    post_reboot_mark = console.get_clean_mark()

    # 2) After reboot, device should report awaiting validation within 3 minutes
    pos = wait_for_status("AWAITING_VALIDATION", post_reboot_mark, timeout_s=180)

    # 3) Allow up to 6 minutes to mark valid and become UP_TO_DATE (strict linear order)
    pos = wait_for_status("UP_TO_DATE", pos, timeout_s=360)


