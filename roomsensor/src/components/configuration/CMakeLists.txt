idf_component_register(
    SRCS
        "ConfigurationManager.cpp"
        "WifiConfig.cpp"
        "TagsConfig.cpp"
        "LEDConfig.cpp"
        "A2DConfig.cpp"
        "IOConfig.cpp"
        "MotionConfig.cpp"
    INCLUDE_DIRS "." "../common"
    REQUIRES nvs_flash json
)


