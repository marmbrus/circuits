#include "sensors.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"
#include "bq27441.h"
#include "i2c_master_ext.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "wifi.h"
#include "led_control.h"

static const char *TAG = "sensors";

static BatteryGaugeData battery_data; // Global variable to store battery data
static i2c_master_bus_handle_t i2c_handle; // Declare i2c_handle as a static variable

// Global variable for battery SOC
uint8_t g_battery_soc = 100; // Default SOC to 100%

// Declare the sensor_task function before its usage
static void sensor_task(void* pvParameters);

esp_err_t sensors_init(void)
{
    esp_err_t err = i2c_master_init(&i2c_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C master initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    // Pass the initialized i2c_handle to bq27441
    bq27441_set_i2c_handle(i2c_handle);

    // Create the sensor task
    BaseType_t ret = xTaskCreate(
        sensor_task,
        "sensor_task",
        SENSOR_TASK_STACK_SIZE,           // Stack size in words
        NULL,           // Task parameters
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
    ESP_LOGI(TAG, "Sensor task started");

    // Retrieve the MQTT client handle
    esp_mqtt_client_handle_t mqtt_client = get_mqtt_client();

    // Main sensor reading loop
    while (1) {
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

            // Get MAC address and construct the topic
            uint8_t mac[6];
            esp_wifi_get_mac(WIFI_IF_STA, mac);
            char topic[64];
            snprintf(topic, sizeof(topic), "surprise/%02x%02x%02x%02x%02x%02x/battery",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

            // Publish the JSON string to the MQTT topic
            esp_mqtt_client_publish(mqtt_client, topic, json_string, 0, 1, 0);

            // Free the JSON string and object
            cJSON_free(json_string);
            cJSON_Delete(json);
        } else {
            ESP_LOGE(TAG, "Failed to read BQ27441 data: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_TASK_INTERVAL_MS)); // Sleep for 10 seconds
    }
}