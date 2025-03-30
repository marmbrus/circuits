#include <stdio.h>
#include "i2c_master_ext.h"
#include "led_control.h"
#include "tas5825m.h"
#include "esp_spiffs.h"
#include "esp_log.h"

void app_main(void)
{
    // Initialize I2C master bus handle
    i2c_master_bus_handle_t i2c_handle = NULL;
    esp_err_t ret = i2c_master_init(&i2c_handle);

    if (ret != ESP_OK) {
        printf("Failed to initialize I2C master bus: %s\n", esp_err_to_name(ret));
        return;
    }

    // Scan the I2C bus for devices
    ret = i2c_master_bus_detect_devices(i2c_handle);
    if (ret != ESP_OK) {
        printf("Failed to scan I2C bus: %s\n", esp_err_to_name(ret));
        return;
    }

    // Initialize LED control
    ret = led_control_init();
    if (ret != ESP_OK) {
        printf("Failed to initialize LED control: %s\n", esp_err_to_name(ret));
        return;
    }

    // Set LED to green
    led_set_color(0, 255, 0);

    // Initialize TAS5825M
    ret = tas5825m_init(i2c_handle);
    if (ret != ESP_OK) {
        printf("Failed to initialize TAS5825M: %s\n", esp_err_to_name(ret));
        return;
    }

    // Add after other initializations, before TAS5825M init
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret_spiffs = esp_vfs_spiffs_register(&conf);
    if (ret_spiffs != ESP_OK) {
        ESP_LOGE("main", "Failed to mount SPIFFS (%s)", esp_err_to_name(ret_spiffs));
        return;
    }

    // Change the test tone line to WAV playback
    printf("Playing WAV file...\n");
    ESP_ERROR_CHECK(tas5825m_play_wav());

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}