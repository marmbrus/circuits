idf_component_register(
    SRCS 
        "main.cpp" 
        "wifi.cpp" 
        "sensors.cpp"
    INCLUDE_DIRS "."
    REQUIRES i2c lights
)
