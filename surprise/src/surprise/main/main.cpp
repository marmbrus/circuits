#include <stdio.h>
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi.h"
#include "led_control.h"
#include "sensors.h"
#include "io_manager.h"
#include "logging_app.h"

static const char *TAG = "main";

extern "C" void app_main()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Create application instance
    LoggingApp app;

    // Initialize IO manager with the application
    IOManager ioManager(&app);

    // Initialize other components
    led_control_init();
    wifi_mqtt_init();
    ESP_ERROR_CHECK(sensors_init());

    // Main event loop
    while(1) {
        ioManager.processEvents();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}