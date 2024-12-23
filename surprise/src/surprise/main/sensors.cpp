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
#include "wifi.h"
#include "led_control.h"
#include "lis2dh.h"
#include "io_manager.h"
#include "button_event.h"

static const char *TAG = "sensors";

static BatteryGaugeData battery_data; // Global variable to store battery data
static i2c_master_bus_handle_t i2c_handle; // Declare i2c_handle as a static variable
static bool accelerometer_initialized = false; // Add flag to track accelerometer state

// Global variable for battery SOC
uint8_t g_battery_soc = 100; // Default SOC to 100%

// Add at the top with other static variables
static float last_x = 0, last_y = 0, last_z = 0;  // For tracking movement
static const float ORIENTATION_THRESHOLD = 0.8f;    // Consider axis aligned if > 0.8g
static const float MOVEMENT_THRESHOLD = 0.1f;       // Minimum change to consider movement

// Enum for orientation
typedef enum {
    ORIENTATION_UP,
    ORIENTATION_DOWN,
    ORIENTATION_LEFT,
    ORIENTATION_RIGHT,
    ORIENTATION_TOP,
    ORIENTATION_BOTTOM,
    ORIENTATION_UNKNOWN
} device_orientation_t;

static device_orientation_t current_orientation = ORIENTATION_UNKNOWN;

// Function to determine orientation from accelerometer data
static device_orientation_t determine_orientation(float x, float y, float z) {
    // Check if any axis has a strong enough reading
    if (fabsf(x) > ORIENTATION_THRESHOLD) {
        return (x > 0) ? ORIENTATION_TOP : ORIENTATION_BOTTOM;  // X axis for top/bottom
    }
    if (fabsf(y) > ORIENTATION_THRESHOLD) {
        return (y > 0) ? ORIENTATION_RIGHT : ORIENTATION_LEFT;  // Y axis for left/right
    }
    if (fabsf(z) > ORIENTATION_THRESHOLD) {
        return (z > 0) ? ORIENTATION_UP : ORIENTATION_DOWN;     // Z axis remains up/down
    }
    return ORIENTATION_UNKNOWN;
}

// Function to check if movement is significant
static bool is_significant_movement(float x, float y, float z) {
    bool significant = (
        fabsf(x - last_x) > MOVEMENT_THRESHOLD ||
        fabsf(y - last_y) > MOVEMENT_THRESHOLD ||
        fabsf(z - last_z) > MOVEMENT_THRESHOLD
    );

    last_x = x;
    last_y = y;
    last_z = z;

    return significant;
}

// Declare the sensor_task function before its usage
static void sensor_task(void* pvParameters);

esp_err_t sensors_init(IOManager* ioManager)
{
    esp_err_t err = i2c_master_init(&i2c_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C master initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    // Initialize BQ27441 first
    bq27441_set_i2c_handle(i2c_handle);

    // Initialize LIS2DH12 with retry mechanism
    int retry_count = 0;
    const int max_retries = 3;

    while (retry_count < max_retries) {
        err = lis2dh12_init(i2c_handle);
        if (err == ESP_OK) {
            // Configure the sensor only if initialization succeeded
            err = lis2dh12_set_data_rate(LIS2DH12_ODR_50Hz);
            if (err == ESP_OK) {
                err = lis2dh12_set_scale(LIS2DH12_2G);
            }
            if (err == ESP_OK) {
                err = lis2dh12_set_mode(LIS2DH12_HR_12BIT);
            }
            if (err == ESP_OK) {
                // Only configure normal mode initially
                err = lis2dh12_configure_normal_mode();
                if (err == ESP_OK) {
                    accelerometer_initialized = true;
                    ESP_LOGI(TAG, "LIS2DH12 initialized successfully in normal mode");
                    break;
                }
            }
        }

        ESP_LOGW(TAG, "Failed to initialize LIS2DH12 (attempt %d/%d): %s",
                 retry_count + 1, max_retries, esp_err_to_name(err));
        retry_count++;
        vTaskDelay(pdMS_TO_TICKS(1000)); // Wait 1 second between retries
    }

    if (!accelerometer_initialized) {
        ESP_LOGE(TAG, "Failed to initialize LIS2DH12 after %d attempts", max_retries);
        // Continue anyway, just without accelerometer functionality
    }

    // Create the sensor task
    BaseType_t ret = xTaskCreate(
        sensor_task,
        "sensor_task",
        SENSOR_TASK_STACK_SIZE,           // Stack size in words
        ioManager,           // Pass IOManager as task parameter
        SENSOR_TASK_PRIORITY,              // Task priority
        NULL           // Task handle
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sensor task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void sensor_task(void* pvParameters)
{
    IOManager* ioManager = static_cast<IOManager*>(pvParameters);
    ESP_LOGI(TAG, "Sensor task started");

    // Retrieve the MQTT client handle
    esp_mqtt_client_handle_t mqtt_client = get_mqtt_client();

    // Pass the initialized i2c_handle to bq27441
    bq27441_set_i2c_handle(i2c_handle);

    // Only initialize movement interrupt if accelerometer was initialized
    if (accelerometer_initialized) {
        ioManager->initMovementInterrupt();
    }

    // Setup all the timing intervals
    const TickType_t accel_interval = pdMS_TO_TICKS(1000);  // Read accelerometer every 1 second
    const TickType_t mqtt_publish_interval = pdMS_TO_TICKS(10000);  // Publish sensor data every 10 seconds

    TickType_t last_accel_time = xTaskGetTickCount();
    TickType_t last_mqtt_publish = xTaskGetTickCount();
    TickType_t last_battery_publish = xTaskGetTickCount();  // Add this

    // Main sensor reading loop
    while (1) {
        TickType_t now = xTaskGetTickCount();

        // Read accelerometer every second
        if ((now - last_accel_time) >= accel_interval) {
            if (accelerometer_initialized) {
                lis2dh12_accel_t accel_data;
                esp_err_t accel_ret = lis2dh12_get_accel(&accel_data);
                if (accel_ret == ESP_OK) {
                    // Check orientation
                    device_orientation_t new_orientation = determine_orientation(
                        accel_data.x, accel_data.y, accel_data.z);

                    // If orientation changed or if it's unknown and there's significant movement
                    if (new_orientation != current_orientation &&
                        (new_orientation != ORIENTATION_UNKNOWN ||
                         is_significant_movement(accel_data.x, accel_data.y, accel_data.z))) {

                        // Initialize ButtonEvent with a default value
                        ButtonEvent evt = ButtonEvent::ORIENTATION_UNKNOWN;

                        // Map the orientation to ButtonEvent
                        switch (new_orientation) {
                            case ORIENTATION_UP:     evt = ButtonEvent::ORIENTATION_UP; break;
                            case ORIENTATION_DOWN:   evt = ButtonEvent::ORIENTATION_DOWN; break;
                            case ORIENTATION_LEFT:   evt = ButtonEvent::ORIENTATION_LEFT; break;
                            case ORIENTATION_RIGHT:  evt = ButtonEvent::ORIENTATION_RIGHT; break;
                            case ORIENTATION_TOP:    evt = ButtonEvent::ORIENTATION_TOP; break;
                            case ORIENTATION_BOTTOM: evt = ButtonEvent::ORIENTATION_BOTTOM; break;
                            case ORIENTATION_UNKNOWN: evt = ButtonEvent::ORIENTATION_UNKNOWN; break;
                        }

                        ioManager->sendEvent(evt);
                        current_orientation = new_orientation;

                        // Log the orientation change
                        const char* orientation_str[] = {
                            "Up", "Down", "Left", "Right", "Top", "Bottom", "Unknown"
                        };
                        ESP_LOGI(TAG, "Orientation changed to: %s", orientation_str[new_orientation]);
                    }

                    // Log the accelerometer values locally every second
                    ESP_LOGI(TAG, "Accelerometer: X=%.3fg, Y=%.3fg, Z=%.3fg",
                            accel_data.x, accel_data.y, accel_data.z);

                    // Publish to MQTT every 10 seconds
                    if ((now - last_mqtt_publish) >= mqtt_publish_interval) {
                        cJSON *accel_json = cJSON_CreateObject();
                        cJSON_AddNumberToObject(accel_json, "x", accel_data.x);
                        cJSON_AddNumberToObject(accel_json, "y", accel_data.y);
                        cJSON_AddNumberToObject(accel_json, "z", accel_data.z);

                        char *accel_string = cJSON_Print(accel_json);
                        publish_to_topic("accelerometer", accel_string);

                        cJSON_free(accel_string);
                        cJSON_Delete(accel_json);

                        last_mqtt_publish = now;
                    }
                }
            }
            last_accel_time = now;
        }

        // Read and publish battery data every 10 seconds
        if ((now - last_battery_publish) >= mqtt_publish_interval) {
            esp_err_t ret = bq27441_read_data(&battery_data);
            if (ret == ESP_OK) {
                // Update the global SOC variable
                g_battery_soc = battery_data.soc;

                // Create a JSON object
                cJSON *json = cJSON_CreateObject();
                cJSON_AddNumberToObject(json, "temperature", battery_data.temperature - 273);
                cJSON_AddNumberToObject(json, "voltage", battery_data.voltage);
                cJSON_AddNumberToObject(json, "flags", battery_data.flags);
                cJSON_AddNumberToObject(json, "nominal_capacity", battery_data.nominal_capacity);
                cJSON_AddNumberToObject(json, "available_capacity", battery_data.available_capacity);
                cJSON_AddNumberToObject(json, "remaining_capacity", battery_data.remaining_capacity);
                cJSON_AddNumberToObject(json, "full_capacity", battery_data.full_capacity);
                cJSON_AddNumberToObject(json, "average_current", battery_data.average_current);
                cJSON_AddNumberToObject(json, "standby_current", battery_data.standby_current);
                cJSON_AddNumberToObject(json, "max_current", battery_data.max_current);
                cJSON_AddNumberToObject(json, "average_power", battery_data.average_power);
                cJSON_AddNumberToObject(json, "soc", battery_data.soc);
                cJSON_AddNumberToObject(json, "internal_temperature", battery_data.internal_temperature - 273);
                cJSON_AddNumberToObject(json, "soh", battery_data.soh);
                cJSON_AddNumberToObject(json, "remaining_capacity_unfiltered", battery_data.remaining_capacity_unfiltered);
                cJSON_AddNumberToObject(json, "remaining_capacity_filtered", battery_data.remaining_capacity_filtered);
                cJSON_AddNumberToObject(json, "full_capacity_unfiltered", battery_data.full_capacity_unfiltered);
                cJSON_AddNumberToObject(json, "full_capacity_filtered", battery_data.full_capacity_filtered);
                cJSON_AddNumberToObject(json, "soc_unfiltered", battery_data.soc_unfiltered);

                // Convert JSON object to string
                char *json_string = cJSON_Print(json);

                publish_to_topic("battery", json_string);

                cJSON_free(json_string);
                cJSON_Delete(json);
            } else {
                ESP_LOGE(TAG, "Failed to read BQ27441 data: %s", esp_err_to_name(ret));
            }
            last_battery_publish = now;
        }

        vTaskDelay(pdMS_TO_TICKS(100));  // Short delay to prevent tight loop
    }
}