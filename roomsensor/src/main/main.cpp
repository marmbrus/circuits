#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "wifi.h"
#include "led_control.h"
#include "cJSON.h"
#include "communication.h"
#include "i2c.h"

static const char* TAG = "main";

extern "C" void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize LED control
    led_control_init();

    // Initialize WiFi and MQTT
    wifi_mqtt_init();

    // Initialize tag system - if this fails, set test tags
    if (initialize_tag_system() != ESP_OK) {
        ESP_LOGW(TAG, "Setting test device tags since they weren't found in NVS");
        set_device_tags_for_testing();
    }

    // Initialize the metrics reporting system (both queue and background task)
    if (initialize_metrics_system() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize metrics system");
    } else {
        ESP_LOGI(TAG, "Metrics reporting system started successfully");
    }

    // Initialize I2C subsystem
    if (!init_i2c()) {
        ESP_LOGE(TAG, "Failed to initialize I2C subsystem");
    } else {
        ESP_LOGI(TAG, "I2C subsystem initialized successfully");
    }

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}