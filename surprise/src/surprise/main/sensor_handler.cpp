#include "sensor_handler.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"
#include <cstring>

static const char *TAG = "sensor_handler";
static i2c_master_dev_handle_t i2c_device = NULL;
static i2c_master_bus_handle_t i2c_bus = NULL;

static void sensor_task(void* pvParameters)
{
    ESP_LOGI(TAG, "Sensor task started");

    // Scan I2C bus for devices
    printf("Scanning I2C bus...\n");
    for (uint8_t i = 1; i < 127; i++) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = i,
            .scl_speed_hz = I2C_MASTER_FREQ_HZ,
        };

        esp_err_t ret = i2c_master_probe(i2c_bus, dev_cfg.device_address, -1);
        if (ret == ESP_OK) {
            printf("Found device at address 0x%02x\n", i);
        }
    }

    // Main sensor reading loop
    while (1) {
        // TODO: Implement sensor reading logic
        vTaskDelay(pdMS_TO_TICKS(SENSOR_TASK_INTERVAL_MS));
    }
}

static esp_err_t i2c_master_init(void)
{
    // Initialize I2C bus configuration
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,                    // i2c_port
        .sda_io_num = (gpio_num_t)I2C_MASTER_SDA_IO,  // sda_io_num
        .scl_io_num = (gpio_num_t)I2C_MASTER_SCL_IO,  // scl_io_num
        .clk_source = I2C_CLK_SRC_DEFAULT,            // clk_source
        .glitch_ignore_cnt = 7,                       // glitch_ignore_cnt
        .flags = {
            .enable_internal_pullup = true
        }
    };

    // Create I2C master bus
    esp_err_t err = i2c_new_master_bus(&bus_config, &i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus creation failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t sensor_handler_init(void)
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
        SENSOR_TASK_STACK_SIZE,
        NULL,
        SENSOR_TASK_PRIORITY,
        NULL
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create sensor task");
        return ESP_FAIL;
    }

    return ESP_OK;
}