#include "status_led.h"
#include "ConfigurationManager.h"
#include "WifiConfig.h"
#include "system_state.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "StatusLED";
static int status_gpio = -1;

// Forward declaration
void status_led_task(void* pvParameters);

esp_err_t init_status_led() {
    config::ConfigurationManager& cfg = config::GetConfigurationManager();
    const auto& wifi_cfg = cfg.wifi();

    if (wifi_cfg.has_status_gpio()) {
        status_gpio = wifi_cfg.status_gpio();
        ESP_LOGI(TAG, "Status LED configured on GPIO %d", status_gpio);

        gpio_config_t io_conf;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << status_gpio);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure status GPIO: %s", esp_err_to_name(err));
            return err;
        }

        // Initial state: solid ON (active low)
        gpio_set_level((gpio_num_t)status_gpio, 0);

        xTaskCreate(status_led_task, "status_led_task", 2048, NULL, 5, NULL);
    } else {
        ESP_LOGI(TAG, "Status LED not configured.");
    }

    return ESP_OK;
}

void status_led_task(void* pvParameters) {
    while (1) {
        SystemState state = get_system_state();
        switch (state) {
            case WIFI_CONNECTING:
            case WIFI_CONNECTED_MQTT_CONNECTING:
                // Gentle pulsing (slow blink)
                gpio_set_level((gpio_num_t)status_gpio, 0); // ON
                vTaskDelay(pdMS_TO_TICKS(500));
                gpio_set_level((gpio_num_t)status_gpio, 1); // OFF
                vTaskDelay(pdMS_TO_TICKS(500));
                break;
            case FULLY_CONNECTED:
                // OFF
                gpio_set_level((gpio_num_t)status_gpio, 1);
                vTaskDelay(pdMS_TO_TICKS(1000)); // Check state every second
                break;
            case MQTT_ERROR_STATE:
                // Rapid blinking
                gpio_set_level((gpio_num_t)status_gpio, 0); // ON
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_set_level((gpio_num_t)status_gpio, 1); // OFF
                vTaskDelay(pdMS_TO_TICKS(100));
                break;
            case OTA_UPDATING:
                 // Rapid blinking
                gpio_set_level((gpio_num_t)status_gpio, 0); // ON
                vTaskDelay(pdMS_TO_TICKS(50));
                gpio_set_level((gpio_num_t)status_gpio, 1); // OFF
                vTaskDelay(pdMS_TO_TICKS(50));
                break;
            default:
                // Default to OFF and check state
                gpio_set_level((gpio_num_t)status_gpio, 1);
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
        }
    }
}


