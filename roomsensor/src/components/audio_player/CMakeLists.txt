idf_component_register(
    SRCS "AudioPlayer.cpp" "MicStream.cpp" "audio_elements.c" "audio_io.c" "mic_codec.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "../common"
    REQUIRES configuration audio_pipeline
    PRIV_REQUIRES ble audio_stream esp-adf-libs audio_board esp_driver_gpio esp_driver_i2s esp_timer mbedtls
)
