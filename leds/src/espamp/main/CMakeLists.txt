idf_component_register(
    SRCS "main.c" "i2c_master_ext.c" "led_control.c" "tas5825m.c"
    INCLUDE_DIRS "."
    REQUIRES driver led_strip esp_timer spiffs
)

target_link_libraries(${COMPONENT_LIB} PRIVATE m)