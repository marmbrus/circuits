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

# Generate build_timestamp.h on every build with current UTC epoch seconds using Python to avoid shell $ escaping
set(BUILD_TIMESTAMP_HEADER "${CMAKE_CURRENT_BINARY_DIR}/build_timestamp.h")

add_custom_command(
    OUTPUT ${BUILD_TIMESTAMP_HEADER}
    COMMAND ${CMAKE_COMMAND} -DOUT=${BUILD_TIMESTAMP_HEADER} -P ${CMAKE_CURRENT_SOURCE_DIR}/gen_build_timestamp.cmake
    COMMENT "Generating build_timestamp.h with current UTC epoch"
    VERBATIM
)

add_custom_target(update_build_timestamp ALL DEPENDS ${BUILD_TIMESTAMP_HEADER})

add_dependencies(${COMPONENT_LIB} update_build_timestamp)

target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
