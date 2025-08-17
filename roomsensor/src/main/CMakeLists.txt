idf_component_register(
    SRCS
        "main.cpp"
        "wifi.cpp"
        "metrics.cpp"
        "http.cpp"
        "ota.cpp"
        "telemetry.cpp"
    INCLUDE_DIRS "."
    REQUIRES i2c lights driver nvs_flash mqtt json esp_wifi esp_app_format esp_http_server esp_http_client esp_https_ota app_update console vfs serial_console configuration
)

# Add build timestamp (in UTC seconds since epoch) at compile time
execute_process(
    COMMAND bash -c "date -u +%s"
    OUTPUT_VARIABLE BUILD_TIMESTAMP
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

message(STATUS "Embedding build timestamp (UTC): ${BUILD_TIMESTAMP}")

# Explicitly set the timestamp in the CMake cache so deploy_ota.sh can read it
set(BUILD_TIMESTAMP ${BUILD_TIMESTAMP} CACHE STRING "Firmware build timestamp (UTC epoch)" FORCE)

# Add the timestamp as a compile definition
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    "BUILD_TIMESTAMP=${BUILD_TIMESTAMP}"
)
