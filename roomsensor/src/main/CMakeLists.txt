idf_component_register(
    SRCS
        "main.cpp"
        "wifi.cpp"
        "tags.cpp"
        "metrics.cpp"
        "http.cpp"
        "ota.cpp"
    INCLUDE_DIRS "."
    REQUIRES i2c lights driver nvs_flash mqtt json esp_wifi esp_app_format esp_http_server esp_http_client esp_https_ota app_update
)
