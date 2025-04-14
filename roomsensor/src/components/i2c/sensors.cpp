#include <math.h>
#include "sensors.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "config.h"
#include "bq27441.h"
#include "i2c_master_ext.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "lis2dh.h"

#include "communication.h"
static const char *TAG = "sensors";

// Define the global battery SOC variable
uint8_t g_battery_soc = 100;  // Default to 100%

// Callback function pointers
static movement_callback_t movement_callback = NULL;
static orientation_callback_t orientation_callback = NULL;

static i2c_master_bus_handle_t i2c_handle;

// Function declarations
static void sensor_task(void* pvParameters);
static esp_err_t read_battery_status(void);

static esp_err_t bq27441_init(void) {
    ESP_LOGI(TAG, "Initializing BQ27441 battery gauge");
    esp_err_t err = bq27441_init_with_handle(i2c_handle);
    return err;
}

static esp_err_t read_battery_status(void) {
    // If battery is not available, skip reading
    if (!bq27441_is_available()) {
        return ESP_OK;
    }
    
    // Read battery data
    BatteryGaugeData data;
    esp_err_t err = bq27441_read_data(&data);
    
    if (err == ESP_OK) {
        g_battery_soc = data.soc;
        
        // Publish battery data to MQTT
        cJSON *battery_json = cJSON_CreateObject();
        cJSON_AddNumberToObject(battery_json, "soc", data.soc);
        cJSON_AddNumberToObject(battery_json, "voltage", data.voltage);
        cJSON_AddNumberToObject(battery_json, "current", data.max_current);
        cJSON_AddNumberToObject(battery_json, "capacity", data.remaining_capacity);
        cJSON_AddNumberToObject(battery_json, "full_capacity", data.full_capacity);
        cJSON_AddNumberToObject(battery_json, "temperature", data.temperature);
        
        char *battery_string = cJSON_Print(battery_json);
        publish_to_topic("battery", battery_string);
        
        cJSON_free(battery_string);
        cJSON_Delete(battery_json);
    } else {
        // If reading fails, mark battery as unavailable
        bq27441_set_availability(false);
        
        // Avoid flooding logs with errors - only publish unavailable status once
        static bool unavailable_reported = false;
        if (!unavailable_reported) {
            // Publish battery unavailable status
            cJSON *battery_json = cJSON_CreateObject();
            cJSON_AddBoolToObject(battery_json, "available", false);
            
            char *battery_string = cJSON_Print(battery_json);
            publish_to_topic("battery", battery_string);
            
            cJSON_free(battery_string);
            cJSON_Delete(battery_json);
            
            ESP_LOGW(TAG, "Battery gauge not available, disabling battery monitoring");
            unavailable_reported = true;
        }
    }
    
    return ESP_OK;
}

static void sensor_task(void* pvParameters) {
    TickType_t last_publish_time = 0;
    const TickType_t publish_interval = pdMS_TO_TICKS(SENSOR_TASK_INTERVAL_MS);
    
    // Wait a bit before starting to read sensors
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Fix missing initializers warning
    lis2dh12_accel_t accel_data = {.x = 0, .y = 0, .z = 0};
    
    while (1) {
        // Try to read accelerometer
        esp_err_t accel_err = lis2dh12_get_accel(&accel_data);
        
        if (accel_err == ESP_OK) {
            float x = accel_data.x;
            float y = accel_data.y;
            float z = accel_data.z;
            
            // Check for movement
            if (is_significant_movement(x, y, z) && movement_callback != NULL) {
                movement_callback();
            }
            
            // Check orientation
            device_orientation_t new_orientation = determine_orientation(x, y, z);
            if (new_orientation != current_orientation && orientation_callback != NULL) {
                orientation_callback(new_orientation);
                current_orientation = new_orientation;
            }
            
            // Periodically publish accelerometer data
            TickType_t current_time = xTaskGetTickCount();
            if (current_time - last_publish_time >= publish_interval) {
                cJSON *accel_json = cJSON_CreateObject();
                cJSON_AddNumberToObject(accel_json, "x", x);
                cJSON_AddNumberToObject(accel_json, "y", y);
                cJSON_AddNumberToObject(accel_json, "z", z);
                
                char *accel_string = cJSON_Print(accel_json);
                publish_to_topic("accelerometer", accel_string);
                
                cJSON_free(accel_string);
                cJSON_Delete(accel_json);
                
                last_publish_time = current_time;
            }
        } else {
            ESP_LOGW(TAG, "Failed to read accelerometer: %s", esp_err_to_name(accel_err));
        }
        
        // Try to read battery but don't fail if it doesn't work
        esp_err_t bat_err = read_battery_status();
        if (bat_err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read battery: %s", esp_err_to_name(bat_err));
        }
        
        vTaskDelay(pdMS_TO_TICKS(SENSOR_TASK_PERIOD_MS));
    }
}

esp_err_t sensors_init_with_callbacks(movement_callback_t movement_cb, orientation_callback_t orientation_cb) {
    // Store the callbacks
    movement_callback = movement_cb;
    orientation_callback = orientation_cb;
    
    // Initialize I2C using the existing function from i2c_master_ext.c
    ESP_LOGI(TAG, "Initializing sensors");
    
    // Initialize I2C bus with the helper function that uses constants from config.h
    esp_err_t err = i2c_master_init(&i2c_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C: %s", esp_err_to_name(err));
        return err;
    }
    
    // Detect I2C devices for debugging
    ESP_LOGI(TAG, "Scanning I2C bus for devices...");
    i2c_master_bus_detect_devices(i2c_handle);
    
    // Initialize the LIS2DH12 accelerometer
    err = lis2dh12_init(i2c_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LIS2DH accelerometer: %s", esp_err_to_name(err));
        // Continue anyway
    }
    
    // Initialize battery gauge with the shared I2C handle
    err = bq27441_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize BQ27441 battery gauge: %s", esp_err_to_name(err));
        // Continue anyway
    }

    // Create sensor task
    BaseType_t xReturned = xTaskCreate(
        sensor_task,
        "sensor_task",
        4096,
        NULL,
        5,
        NULL
    );

    if (xReturned != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sensor task");
        return ESP_FAIL;
    }

    return ESP_OK;
}