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
#include "ButtonsPuzzleApp.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "config.h"

static const char *TAG = "main";

void set_5V_pin(bool enable) {
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << GPIO_NUM_6);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);

    gpio_set_level(GPIO_NUM_6, enable ? 1 : 0);
}

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
    ButtonsPuzzleApp app;

    // Initialize IO manager with the application
    IOManager ioManager(&app);

    // Enable 5V pin
    set_5V_pin(true);

    // Initialize other components
    led_control_init();
    wifi_mqtt_init();
    ESP_ERROR_CHECK(sensors_init(&ioManager));

    // Main event loop
    TickType_t lastEventTime = xTaskGetTickCount();

    while(1) {
        if (ioManager.processEvents()) {
            lastEventTime = xTaskGetTickCount();
        }

        if ((xTaskGetTickCount() - lastEventTime) * portTICK_PERIOD_MS > INACTIVITY_THRESHOLD_MS) {
            ESP_LOGI(TAG, "Entering deep sleep mode due to inactivity");

            // Turn off all LEDs and stop the update task
            led_control_clear();
            led_control_stop();

            // Configure wakeup sources for low signal using button GPIOs
            esp_sleep_enable_ext1_wakeup(
                (1ULL << BUTTON1_GPIO) |
                (1ULL << BUTTON2_GPIO) |
                (1ULL << BUTTON3_GPIO) |
                (1ULL << BUTTON4_GPIO),
                ESP_EXT1_WAKEUP_ALL_LOW
            );

            // Enter deep sleep
            esp_deep_sleep_start();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}