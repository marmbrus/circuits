idf_component_register(
    SRCS "BLEScanner.cpp"
    INCLUDE_DIRS "." "../common"
    REQUIRES configuration ble_logic
    PRIV_REQUIRES bt json esp_timer esp_wifi
)
