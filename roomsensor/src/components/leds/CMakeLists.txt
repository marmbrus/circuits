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
        "PositionTestPattern.cpp"
        "ClockPattern.cpp"
        "CalendarPattern.cpp"
        "font6x6.cpp"

        # New internal mapper/encoder implementations
        "LEDWireEncoderWS2812.cpp"
        "LEDWireEncoderFlipdot.cpp"
        "LEDWireEncoderSK6812.cpp"
        "LEDWireEncoderWS2814.cpp"
        
        # Adapter (header-only)
        
    INCLUDE_DIRS "."
    REQUIRES json freertos configuration espressif__led_strip esp_timer
    PRIV_REQUIRES esp_driver_gpio
)


