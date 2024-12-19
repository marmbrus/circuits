#include "bq27441.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "i2c_master_ext.h"
#include "config.h"

static const char *TAG = "bq27441";

static i2c_master_bus_handle_t i2c_handle; // Declare i2c_handle as a static variable

void bq27441_set_i2c_handle(i2c_master_bus_handle_t handle) {
    i2c_handle = handle;
}

// Function to read a 16-bit register from the BQ27441
static esp_err_t read_bq27441_register(uint8_t reg, uint16_t *value) {
    i2c_master_dev_handle_t dev_handle;
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BQ27441_I2C_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t err = i2c_master_bus_add_device(i2c_handle, &dev_config, &dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t data[2];
    err = i2c_master_bus_read_byte16(dev_handle, reg, (i2c_uint16_t *)data);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read register 0x%02x: %s", reg, esp_err_to_name(err));
    } else {
        *value = (data[1] << 8) | data[0]; // Combine the two bytes into a 16-bit value
    }

    i2c_master_bus_rm_device(dev_handle);
    return err;
}

esp_err_t bq27441_read_data(BatteryGaugeData *battery_data) {
    esp_err_t ret;

    ret = read_bq27441_register(BQ27441_COMMAND_TEMP, &battery_data->temperature);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_VOLTAGE, &battery_data->voltage);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_FLAGS, &battery_data->flags);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_NOM_CAPACITY, &battery_data->nominal_capacity);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_AVAIL_CAPACITY, &battery_data->available_capacity);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_REM_CAPACITY, &battery_data->remaining_capacity);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_FULL_CAPACITY, &battery_data->full_capacity);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_AVG_CURRENT, (uint16_t *)&battery_data->average_current);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_STDBY_CURRENT, (uint16_t *)&battery_data->standby_current);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_MAX_CURRENT, (uint16_t *)&battery_data->max_current);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_AVG_POWER, (uint16_t *)&battery_data->average_power);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_SOC, &battery_data->soc);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_INT_TEMP, &battery_data->internal_temperature);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_SOH, &battery_data->soh);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_REM_CAP_UNFL, &battery_data->remaining_capacity_unfiltered);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_REM_CAP_FIL, &battery_data->remaining_capacity_filtered);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_FULL_CAP_UNFL, &battery_data->full_capacity_unfiltered);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_FULL_CAP_FIL, &battery_data->full_capacity_filtered);
    if (ret != ESP_OK) return ret;

    ret = read_bq27441_register(BQ27441_COMMAND_SOC_UNFL, &battery_data->soc_unfiltered);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}