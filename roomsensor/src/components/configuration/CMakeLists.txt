idf_component_register(
    SRCS
        "ConfigurationManager.cpp"
        "WifiConfig.cpp"
        "TagsConfig.cpp"
        "DeviceConfig.cpp"
        "LEDConfig.cpp"
        "A2DConfig.cpp"
        "IOConfig.cpp"
        "MotionConfig.cpp"
        "I2CConfig.cpp"
        "GameOfLifeConfig.cpp"
    INCLUDE_DIRS "." "../common"
    REQUIRES nvs_flash json
)


