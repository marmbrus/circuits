
idf_component_register(
    SRCS
        "audio_init.cpp"
    INCLUDE_DIRS "."
    REQUIRES configuration audio_pipeline audio_stream esp_http_client
    PRIV_REQUIRES esp_driver_i2s freertos
)
