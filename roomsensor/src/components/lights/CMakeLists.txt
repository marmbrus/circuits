idf_component_register(
    SRCS 
        "led_control.cpp"
        "FourColorLights.cpp"
        "ChristmasLights.cpp"
        "NoLights.cpp"
        "ChasingLights.cpp"
        "RainbowLights.cpp"
        "RainbowChasing.cpp"
        "FlashingLights.cpp"
        "PulsingLights.cpp"
        "SolidLights.cpp"
        "ConnectingLights.cpp"
        "ConnectedLights.cpp"
        "DisconnectedLights.cpp"
    INCLUDE_DIRS "." "../common"
    PRIV_REQUIRES driver led_strip esp_common esp_timer
) 