#include "i2c_sensor.h"
#include "lis2dh_sensor.h"
#include "bme280_sensor.h"
#include "i2c_master_ext.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "I2C";

// Array of all possible sensors
static I2CSensor* s_sensors[] = {
    new LIS2DHSensor(),
    new BME280Sensor(),
    // Add more sensors here as they are implemented
};

static const int s_sensor_count = sizeof(s_sensors) / sizeof(s_sensors[0]);
static i2c_master_bus_handle_t s_i2c_bus = nullptr;

// Task handle for the sensor polling task
static TaskHandle_t s_polling_task_handle = nullptr;

// Create a semaphore to signal interrupt events
static SemaphoreHandle_t s_sensorInterruptSemaphore = nullptr;

// Sensor polling task function
static void sensor_polling_task(void* pvParameters) {
    const TickType_t polling_period = pdMS_TO_TICKS(10000); // 10 seconds
    
    while (true) {
        // Block until signaled or the timeout expires
        if (xSemaphoreTake(s_sensorInterruptSemaphore, polling_period) == pdTRUE) {
            ESP_LOGD(TAG, "Woken by interrupt signal, polling sensors now...");
        } else {
            ESP_LOGD(TAG, "Polling interval reached, polling sensors...");
        }
        
        // Poll each initialized sensor
        for (int i = 0; i < s_sensor_count; i++) {
            if (s_sensors[i]->isInitialized()) {
                ESP_LOGD(TAG, "Polling sensor: %s", s_sensors[i]->name().c_str());
                s_sensors[i]->poll();
                // Each sensor now handles events internally in its poll() method
            }
        }
    }
}

bool init_i2c() {
    ESP_LOGI(TAG, "Initializing I2C bus");
    
    // Create semaphore for interrupt signaling
    s_sensorInterruptSemaphore = xSemaphoreCreateBinary();
    if (s_sensorInterruptSemaphore == nullptr) {
        ESP_LOGE(TAG, "Failed to create polling semaphore");
        return false;
    }
    
    // Initialize the I2C bus
    esp_err_t err = i2c_master_init(&s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C master bus: %s", esp_err_to_name(err));
        return false;
    }
    
    // Scan the I2C bus once and try to initialize any recognized sensors
    const uint16_t probe_timeout_ms = 50;
    int found_count = 0;
    int initialized_count = 0;
    
    ESP_LOGI(TAG, "Scanning I2C bus for devices...");
    
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        esp_err_t ret = i2c_master_probe(s_i2c_bus, addr, probe_timeout_ms);
        
        if (ret == ESP_OK) {
            found_count++;
            
            // Check if this address matches any of our known sensors
            bool recognized = false;
            
            for (int i = 0; i < s_sensor_count; i++) {
                if (s_sensors[i]->addr() == addr) {
                    recognized = true;
                    ESP_LOGI(TAG, "Found device at address 0x%02X: %s", addr, s_sensors[i]->name().c_str());
                    
                    // Try to initialize the sensor if it's not already initialized
                    if (!s_sensors[i]->isInitialized()) {
                        // Use the base class init method with bus handle
                        bool initialized = s_sensors[i]->init(s_i2c_bus);
                        
                        if (initialized) {
                            ESP_LOGI(TAG, "Successfully initialized %s", s_sensors[i]->name().c_str());
                            initialized_count++;
                        } else {
                            ESP_LOGW(TAG, "Failed to initialize %s", s_sensors[i]->name().c_str());
                        }
                    } else {
                        ESP_LOGI(TAG, "%s already initialized", s_sensors[i]->name().c_str());
                        initialized_count++;
                    }
                    break;
                }
            }
            
            if (!recognized) {
                ESP_LOGW(TAG, "Found unrecognized device at address 0x%02X", addr);
            }
        }
    }
    
    ESP_LOGI(TAG, "I2C scan complete: %d devices found, %d sensors initialized", 
             found_count, initialized_count);
    
    // Start the sensor polling task if we have at least one initialized sensor
    if (initialized_count > 0) {
        ESP_LOGI(TAG, "Starting sensor polling task");
        BaseType_t task_created = xTaskCreate(
            sensor_polling_task,  // Task function
            "i2c_polling",        // Task name
            4096,                 // Stack size (bytes)
            nullptr,              // Task parameters
            5,                    // Task priority
            &s_polling_task_handle // Task handle
        );
        
        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "Failed to create sensor polling task");
            return false;
        }
        
        ESP_LOGI(TAG, "Sensor polling task started");
    } else {
        ESP_LOGW(TAG, "No sensors initialized, polling task not started");
    }
    
    return true;
}

// Public function to signal the sensor polling task
void signalSensorInterrupt() {
    if (s_sensorInterruptSemaphore) {
        xSemaphoreGive(s_sensorInterruptSemaphore);
    }
} 