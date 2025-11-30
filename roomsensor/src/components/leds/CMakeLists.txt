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
        "SummaryPattern.cpp"
        "SunsetPattern.cpp"
        "MeteorPattern.cpp"
        "SweepPattern.cpp"
        "CrossWipePattern.cpp"
        "CrossFadePattern.cpp"
        "FireworksPattern.cpp"
        "font6x6.cpp"

        # New internal mapper/encoder implementations
        "LEDWireEncoderWS2812.cpp"
        "LEDWireEncoderFlipdot.cpp"
        "LEDWireEncoderSK6812.cpp"
        "LEDWireEncoderWS2814.cpp"
        
        # Adapter (header-only)
        
    INCLUDE_DIRS "."
    # Internal implementation header for RMT LED strip encoder lives under the
    # managed component's src directory, not its public include directory.
    # We add it as a private include so we can build a custom RMT driver on top
    # of the same encoder implementation.
    PRIV_INCLUDE_DIRS
        "../../managed_components/espressif__led_strip/src"

    REQUIRES json freertos configuration espressif__led_strip esp_timer esp_netif
    PRIV_REQUIRES esp_driver_gpio
)


