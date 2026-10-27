# ESP-ADF board definition. Replaces ADF's own audio_board component, which only
# knows Espressif dev boards. Pins come from the "audio" configuration module (NVS),
# so one firmware serves every board.
idf_component_register(
    SRCS "board.c" "board_pins_config.cpp"
    INCLUDE_DIRS "include"
    REQUIRES audio_hal esp_peripherals display_service driver
    PRIV_REQUIRES audio_sal configuration
)
