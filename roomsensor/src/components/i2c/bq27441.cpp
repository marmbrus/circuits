#include "bq27441.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "i2c_master_ext.h"
#include "config.h"
#include <cstring>

static const char *TAG = "bq27441";

static i2c_master_bus_handle_t battery_i2c_handle = NULL;
static i2c_master_dev_handle_t battery_dev_handle = NULL;
static bool battery_available = false;

void bq27441_set_i2c_handle(i2c_master_bus_handle_t handle) {
    battery_i2c_handle = handle;
}

// Function to read a 16-bit register from the BQ27441
static esp_err_t read_bq27441_register(uint8_t reg, uint16_t *value) {
    if (!battery_available || battery_dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[2];
    esp_err_t err = i2c_master_transmit_receive(battery_dev_handle, &reg, 1, data, 2, I2C_XFR_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read register 0x%02x: %s", reg, esp_err_to_name(err));
        return err;
    }
    
    *value = (data[1] << 8) | data[0]; // Combine the two bytes into a 16-bit value
    return ESP_OK;
}

esp_err_t bq27441_read_data(BatteryGaugeData *battery_data) {
    if (!battery_available || battery_dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

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

esp_err_t bq27441_init_with_handle(i2c_master_bus_handle_t i2c_handle) {
    battery_i2c_handle = i2c_handle;
    
    // Device address and parameters - fix struct initialization
    i2c_device_config_t dev_cfg = {};
    memset(&dev_cfg, 0, sizeof(i2c_device_config_t));
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = BQ27441_I2C_ADDRESS;
    dev_cfg.scl_speed_hz = 100000;
    
    // Only try to add device if we have a valid I2C handle
    if (battery_i2c_handle != NULL) {
        if (battery_dev_handle != NULL) {
            // Device already added
            return ESP_OK;
        }
        
        esp_err_t ret = i2c_master_bus_add_device(battery_i2c_handle, &dev_cfg, &battery_dev_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add BQ27441 device: %s", esp_err_to_name(ret));
            battery_available = false;
            return ret;
        }
        
        // Try to read something to verify device exists
        uint16_t dummy;
        ret = read_bq27441_register(BQ27441_COMMAND_VOLTAGE, &dummy);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "BQ27441 not responding: %s", esp_err_to_name(ret));
            battery_available = false;
            return ret;
        }
        
        battery_available = true;
        ESP_LOGI(TAG, "BQ27441 battery gauge initialized successfully");
    } else {
        battery_available = false;
        ESP_LOGE(TAG, "Invalid I2C handle for BQ27441");
    }
    
    return ESP_OK;
}

bool bq27441_is_available(void) {
    return battery_available;
}

void bq27441_set_availability(bool available) {
    battery_available = available;
}