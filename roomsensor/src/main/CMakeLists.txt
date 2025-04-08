idf_component_register(
    SRCS 
        "main.cpp" 
        "wifi.cpp" 
    INCLUDE_DIRS "."
    REQUIRES i2c lights driver nvs_flash mqtt json esp_wifi esp_app_format
)
