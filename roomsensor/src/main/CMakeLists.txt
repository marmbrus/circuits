idf_component_register(
    SRCS
        "main.cpp"
        "wifi.cpp"
        "metrics.cpp"
        "http.cpp"
        "ota.cpp"
        "telemetry.cpp"
        "netlog.cpp"
        "gpio.cpp"
        "filesystem.cpp"
    INCLUDE_DIRS "."
    REQUIRES i2c leds driver nvs_flash mqtt json esp_wifi esp_app_format esp_http_server esp_http_client esp_https_ota app_update console vfs joltwallet__littlefs serial_console configuration
)

# Generate build timestamp directly as compile definition (no header file)
execute_process(
    COMMAND date -u +%s
    OUTPUT_VARIABLE BUILD_TIMESTAMP_EPOCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE DATE_RESULT
)

if(NOT DATE_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to get current timestamp")
endif()

message(STATUS "Embedding build timestamp: ${BUILD_TIMESTAMP_EPOCH}")

# Add timestamp as compile definition
target_compile_definitions(${COMPONENT_LIB} PRIVATE 
    "FIRMWARE_BUILD_TIMESTAMP=${BUILD_TIMESTAMP_EPOCH}"
)
