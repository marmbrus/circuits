idf_component_register(
    SRCS
        "main.cpp"
        "wifi.cpp"
        "metrics.cpp"
        "ota.cpp"
        "telemetry.cpp"
        "netlog.cpp"
        "gpio.cpp"
        "device_cert.cpp"
    INCLUDE_DIRS "."
    REQUIRES i2c leds driver nvs_flash mqtt mbedtls json esp_wifi esp_app_format esp_http_client esp_https_ota app_update console vfs serial_console configuration status_led audio_player ble
    PRIV_REQUIRES espcoredump
)

# Stamp every build with its version and time (build_info.sh): compiled in as build_info.c
# (main/build_info.h) and written to build/build_info.json for deploy_ota.sh's manifest.
set(BUILD_INFO_C "${CMAKE_BINARY_DIR}/build_info.c")
set(BUILD_INFO_JSON "${CMAKE_BINARY_DIR}/build_info.json")
add_custom_target(build_info ALL
    COMMAND bash "${CMAKE_CURRENT_LIST_DIR}/../build_info.sh" "${CMAKE_CURRENT_LIST_DIR}/.."
            "${BUILD_INFO_C}" "${BUILD_INFO_JSON}"
    BYPRODUCTS "${BUILD_INFO_C}" "${BUILD_INFO_JSON}"
    COMMENT "Stamping the build's version and time"
    VERBATIM
)
target_sources(${COMPONENT_LIB} PRIVATE "${BUILD_INFO_C}")
add_dependencies(${COMPONENT_LIB} build_info)
