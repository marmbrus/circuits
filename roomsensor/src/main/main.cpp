#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "driver/gpio.h"
#include "wifi.h"
#include "config.h"
#include "LEDManager.h"
#include "cJSON.h"
#include "communication.h"
#include "i2c.h"
#include "http.h"
#include "ota.h"
#include "console.h"
#include "ConfigurationManager.h"
#include "gpio.h"
#include "filesystem.h"

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

    // Initialize configuration subsystem (loads NVS, publishes current config)
    config::ConfigurationManager& cfg = config::GetConfigurationManager();
    esp_err_t cfg_err = cfg.initialize();
    if (cfg_err != ESP_OK) {
        ESP_LOGE(TAG, "ConfigurationManager initialization failed: %s", esp_err_to_name(cfg_err));
    }
    
    // Initialize LEDs manager
    static leds::LEDManager led_manager;
    if (led_manager.init(cfg) != ESP_OK) {
        ESP_LOGE(TAG, "LEDManager initialization failed");
    }

    // Initialize WiFi and MQTT
    wifi_mqtt_init();

    // Initialize the metrics reporting system (both queue and background task)
    if (initialize_metrics_system() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize metrics system");
    } else {
        ESP_LOGI(TAG, "Metrics reporting system started successfully");
    }

    // Install GPIO ISR service once, before any modules add handlers
    {
        esp_err_t isr_err = gpio_install_isr_service(0);
        if (isr_err != ESP_OK && isr_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(isr_err));
        }
    }

    // Initialize GPIO features (e.g., motion sensor) after metrics system
    if (init_gpio() != ESP_OK) {
        ESP_LOGE(TAG, "GPIO initialization failed");
    }

    // Initialize I2C subsystem
    if (!init_i2c()) {
        ESP_LOGE(TAG, "Failed to initialize I2C subsystem");
    } else {
        ESP_LOGI(TAG, "I2C subsystem initialized successfully");
    }

    // Mount LittleFS (reusing 'storage' partition label)
    if (webfs::init("storage", false) != ESP_OK) {
        ESP_LOGW(TAG, "LittleFS mount failed; web UI may not be available");
    }

    // Start HTTP webserver
    if (start_webserver() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
    } else {
        ESP_LOGI(TAG, "HTTP server started successfully");
    }

    // Initialize OTA update system
    if (ota_init() != ESP_OK) {
        ESP_LOGW(TAG, "OTA initialization failed");
    } else {
        ESP_LOGI(TAG, "OTA system initialized successfully");
    }

    // Initialize interactive console
    initialize_console();
}