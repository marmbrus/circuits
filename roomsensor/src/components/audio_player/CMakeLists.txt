idf_component_register(
    SRCS "AudioPlayer.cpp" "audio_elements.c" "audio_io.c"
    INCLUDE_DIRS "."
    REQUIRES configuration esp_http_server audio_pipeline
    PRIV_REQUIRES audio_stream esp-adf-libs audio_board esp_driver_gpio esp_driver_i2s mbedtls
)
