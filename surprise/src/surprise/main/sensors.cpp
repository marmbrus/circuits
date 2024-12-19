#include "sensors.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"
#include "i2c_master_ext.h"

static const char *TAG = "sensors";

static i2c_master_bus_handle_t i2c_handle; // Declare i2c_handle as a static variable

static void sensor_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Sensor task started");

    // Scan I2C bus for devices using the new driver
    ESP_LOGI(TAG, "Scanning I2C bus...");
    esp_err_t ret = i2c_master_bus_detect_devices(i2c_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to scan I2C bus: %s", esp_err_to_name(ret));
    }

    // Main sensor reading loop
    while (1) {
        // TODO: Implement sensor reading logic
        vTaskDelay(pdMS_TO_TICKS(10000)); // Sleep for 10 seconds
    }
}

static esp_err_t i2c_master_init(void)
{
    i2c_master_bus_config_t i2c_master_cfg = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0, // Default priority
        .trans_queue_depth = 0, // Default queue depth
        .flags = {
            .enable_internal_pullup = true
        }
    };
    esp_err_t err = i2c_new_master_bus(&i2c_master_cfg, &i2c_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C master initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t sensors_init(void)
{
    esp_err_t err = i2c_master_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C master initialization failed: %s", esp_err_to_name(err));
        return err;
    }

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