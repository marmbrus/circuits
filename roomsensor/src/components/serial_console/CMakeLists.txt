idf_component_register(
    SRCS
        "console.cpp"
        "cmd_wifi.c"
        "cmd_nvs.c"
        "cmd_system.c"
        "cmd_system_common.c"
        "cmd_system_sleep.c"
        "cmd_ota.c"
        "gpio.cpp"
    INCLUDE_DIRS "."
    REQUIRES console vfs driver nvs_flash esp_wifi esp_netif esp_event
    PRIV_REQUIRES main
)


