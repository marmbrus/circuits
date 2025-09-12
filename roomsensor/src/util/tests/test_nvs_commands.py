import pytest
from roomsensor_util.serial_console import SerialConsole
import time


def test_nvs_list(console: SerialConsole):
    """
    Tests the 'nvs_list' command to ensure it shows known keys.
    """
    mark = console.get_clean_mark()
    console.write_line("nvs_list")
    console.send_return()

    # The 'wifi' key is created by default at startup.
    assert console.wait_for_clean_after("wifi", mark, 5), "nvs_list did not show 'wifi' key"


def test_nvs_set_get_erase_string(console: SerialConsole):
    """
    Tests setting, getting, and erasing a string value in NVS.
    """
    key = "test_key"
    value = "test_value"

    # 1. Set the value
    mark_set = console.get_clean_mark()
    console.write_line(f"nvs_set {key} str -v {value}")
    console.send_return()
    assert console.wait_for_clean_after(f"Value stored under key '{key}'", mark_set, 5)

    # 2. Get the value and verify it
    mark_get = console.get_clean_mark()
    console.write_line(f"nvs_get {key} str")
    console.send_return()
    assert console.wait_for_clean_after(value, mark_get, 5)

    # 3. Erase the value
    mark_erase = console.get_clean_mark()
    console.write_line(f"nvs_erase {key}")
    console.send_return()
    assert console.wait_for_clean_after(f"Value with key '{key}' erased", mark_erase, 5)

    # 4. Verify that getting the value now fails
    mark_verify_erase = console.get_clean_mark()
    console.write_line(f"nvs_get {key} str")
    console.send_return()
    assert console.wait_for_clean_after("ESP_ERR_NVS_NOT_FOUND", mark_verify_erase, 5)
