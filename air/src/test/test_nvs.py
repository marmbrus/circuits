import re
import time
import uuid
import pytest

from .serial_connection import SerialConnection


@pytest.fixture(scope="function")
def conn_and_ns():
    conn = SerialConnection()
    # Use a unique namespace per test to avoid interference
    ns = f"testns_{uuid.uuid4().hex[:8]}"
    try:
        # Ensure clean start
        conn.send_command(f"nvs_erase_namespace {ns}")
        conn.send_command(f"nvs_namespace {ns}")
        yield conn, ns
    finally:
        # Cleanup: erase namespace and verify
        conn.send_command(f"nvs_erase_namespace {ns}")
        start = conn.send_command(f"nvs_list nvs -n {ns} -t any")
        # There should be no entries from this namespace anymore
        # We assert that no line mentions our namespace/keys
        txt = conn.get_text(start_pos=start)
        assert f"namespace '{ns}'," not in txt
        conn.close()


def _expect_list_contains(conn: SerialConnection, ns: str, key: str, type_str: str) -> None:
    start = conn.send_command(f"nvs_list nvs -n {ns} -t any")
    conn.assert_matches(rf"namespace '{re.escape(ns)}', key '{re.escape(key)}', type '{re.escape(type_str)}'\s*$",
                        start_pos=start, timeout_s=5.0, flags=re.M)


def test_nvs_str_set_get_list_erase(conn_and_ns):
    conn, ns = conn_and_ns
    key = "greeting"
    value = "hello world"

    # Set string
    conn.send_command(f"nvs_set {key} str -v \"{value}\"")

    # Get string
    start = conn.send_command(f"nvs_get {key} str")
    conn.assert_contains(value, start_pos=start, timeout_s=5.0)

    # List shows key with type 'str'
    _expect_list_contains(conn, ns, key, "str")

    # Erase key and verify it's gone
    conn.send_command(f"nvs_erase {key}")
    start = conn.send_command(f"nvs_list nvs -n {ns} -t any")
    txt = conn.get_text(start_pos=start)
    assert key not in txt


def test_nvs_i32_set_get_list(conn_and_ns):
    conn, ns = conn_and_ns
    key = "num_i32"
    value = 123456
    conn.send_command(f"nvs_set {key} i32 -v {value}")
    start = conn.send_command(f"nvs_get {key} i32")
    conn.assert_contains(str(value), start_pos=start, timeout_s=5.0)
    _expect_list_contains(conn, ns, key, "i32")


def test_nvs_u8_set_get_list(conn_and_ns):
    conn, ns = conn_and_ns
    key = "flag_u8"
    value = 255
    conn.send_command(f"nvs_set {key} u8 -v {value}")
    start = conn.send_command(f"nvs_get {key} u8")
    conn.assert_contains(str(value), start_pos=start, timeout_s=5.0)
    _expect_list_contains(conn, ns, key, "u8")


def test_nvs_i64_set_get_list(conn_and_ns):
    conn, ns = conn_and_ns
    key = "big_i64"
    value = 2**40 + 12345
    conn.send_command(f"nvs_set {key} i64 -v {value}")
    start = conn.send_command(f"nvs_get {key} i64")
    conn.assert_contains(str(value), start_pos=start, timeout_s=5.0)
    _expect_list_contains(conn, ns, key, "i64")


def test_nvs_blob_set_get_list(conn_and_ns):
    conn, ns = conn_and_ns
    key = "blob_key"
    hex_data = "deadbeefCAFEBABE".lower()  # hex string, even length
    conn.send_command(f"nvs_set {key} blob -v {hex_data}")
    start = conn.send_command(f"nvs_get {key} blob")
    # nvs_get blob prints hex bytes in lowercase
    conn.assert_contains(hex_data, start_pos=start, timeout_s=5.0)
    _expect_list_contains(conn, ns, key, "blob")


def test_nvs_erase_namespace(conn_and_ns):
    conn, ns = conn_and_ns
    # Populate two keys
    conn.send_command(f"nvs_set k1 str -v \"v1\"")
    conn.send_command(f"nvs_set k2 i32 -v 42")
    # Verify they appear in list
    _expect_list_contains(conn, ns, "k1", "str")
    _expect_list_contains(conn, ns, "k2", "i32")
    # Erase namespace
    conn.send_command(f"nvs_erase_namespace {ns}")
    # Verify gone
    start = conn.send_command(f"nvs_list nvs -n {ns} -t any")
    txt = conn.get_text(start_pos=start)
    assert "k1" not in txt and "k2" not in txt


