idf_component_register(
    SRCS
        "LEDStripRmt.cpp"
        "LEDManager.cpp"
        "OffPattern.cpp"
        "SolidPattern.cpp"
        "FadePattern.cpp"
        "RainbowPattern.cpp"
        "StatusPattern.cpp"
        "GameOfLifePattern.cpp"
        "ChasePattern.cpp"
    INCLUDE_DIRS "."
    REQUIRES json freertos configuration espressif__led_strip esp_timer
    PRIV_REQUIRES esp_driver_gpio
)


