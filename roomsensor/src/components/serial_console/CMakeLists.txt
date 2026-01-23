idf_component_register(SRCS "console.cpp" "console_buffer.c" "cmd_nvs.c" "cmd_wifi.c" "cmd_system.c" "cmd_system_common.c" "cmd_system_sleep.c" "cmd_ota.c" "gpio.cpp" "cmd_hd108.cpp" "cmd_sk6812.cpp" "cmd_tlc59711.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES console nvs_flash spi_flash esp_wifi driver main)


