idf_component_register(
    SRCS
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
        "MarqueePattern.cpp"
        "font6x6.cpp"

        # New implementation files
        "PixelProcessors.cpp"
        "RmtTransmitter.cpp"
        "SpiTransmitter.cpp"
        
    INCLUDE_DIRS "."

    REQUIRES json freertos configuration led_strip esp_timer esp_netif
    PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_driver_rmt
)
