# BLE parsing and windowing with no ESP-IDF dependencies, shared by the ble config module and
# the scanner. host_test/run.sh unit tests it on the host.
idf_component_register(
    SRCS "ble_scan_logic.cpp"
    INCLUDE_DIRS "."
)
