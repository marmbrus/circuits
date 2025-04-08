#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "wifi.h"
#include "sensors.h"
#include "led_control.h"
#include "cJSON.h"
#include "communication.h"

static const char* TAG = "main";

// Callback functions for sensor events
void on_movement_detected() {
    ESP_LOGI(TAG, "Movement detected");
    
    // Publish movement notification
    cJSON *movement_json = cJSON_CreateObject();
    cJSON_AddBoolToObject(movement_json, "detected", true);
    
    char *movement_string = cJSON_Print(movement_json);
    publish_to_topic("movement", movement_string);
    
    cJSON_free(movement_string);
    cJSON_Delete(movement_json);
}

void on_orientation_changed(device_orientation_t orientation) {
    ESP_LOGI(TAG, "Orientation changed to: %d", orientation);
    
    const char* orientation_names[] = {
        "up", "down", "left", "right", "top", "bottom", "unknown"
    };
    
    // Publish orientation change
    cJSON *orientation_json = cJSON_CreateObject();
    cJSON_AddStringToObject(orientation_json, "orientation", 
                          orientation_names[orientation]);
    
    char *orientation_string = cJSON_Print(orientation_json);
    publish_to_topic("orientation", orientation_string);
    
    cJSON_free(orientation_string);
    cJSON_Delete(orientation_json);
}

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
    
    // Initialize sensors with callbacks
    sensors_init_with_callbacks(on_movement_detected, on_orientation_changed);

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}