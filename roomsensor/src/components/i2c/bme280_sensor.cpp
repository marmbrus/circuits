#include "bme280_sensor.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "BME280Sensor";

BME280Sensor::BME280Sensor() : 
    I2CSensor(nullptr),
    _initialized(false),
    _t_fine(0),
    _temperature(0.0f),
    _pressure(0.0f),
    _humidity(0.0f) {
    memset(&_calibData, 0, sizeof(_calibData));
    ESP_LOGD(TAG, "BME280Sensor constructed");
}

uint8_t BME280Sensor::addr() const {
    return BME280_I2C_ADDR;
}

std::string BME280Sensor::name() const {
    return "BME280 Environmental Sensor";
}

bool BME280Sensor::isInitialized() const {
    return _initialized;
}

esp_err_t BME280Sensor::readRegister(uint8_t reg, uint8_t *data, size_t len) {
    // Allow reading chip ID and calibration registers during initialization
    bool is_calibration_reg = (reg == REG_CHIP_ID) || 
                              ((reg >= REG_CALIB_T1_LSB) && (reg <= REG_CALIB_T1_LSB + 26)) ||
                              ((reg >= REG_CALIB_H1) && (reg <= REG_CALIB_H1 + 1)) ||
                              ((reg >= REG_CALIB_H2_LSB) && (reg <= REG_CALIB_H2_LSB + 7));
    
    if (!_initialized && !is_calibration_reg) {
        ESP_LOGW(TAG, "Sensor not initialized, cannot read register 0x%02x", reg);
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = i2c_master_transmit_receive(_dev_handle, &reg, 1, data, len, 100); // 100ms timeout
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read register 0x%02x: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t BME280Sensor::writeRegister(uint8_t reg, uint8_t value) {
    // Allow writing to reset and control registers during initialization
    bool is_control_reg = (reg == REG_RESET) || 
                          (reg == REG_CTRL_HUM) || 
                          (reg == REG_CTRL_MEAS) || 
                          (reg == REG_CONFIG);
    
    if (!_initialized && !is_control_reg) {
        ESP_LOGW(TAG, "Sensor not initialized, cannot write to register 0x%02x", reg);
        return ESP_ERR_INVALID_STATE;
    }
    
    uint8_t write_buf[2] = {reg, value};
    esp_err_t ret = i2c_master_transmit(_dev_handle, write_buf, 2, 100); // 100ms timeout
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write register 0x%02x: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

bool BME280Sensor::init() {
    ESP_LOGE(TAG, "Invalid init() call without bus handle. Use init(i2c_master_bus_handle_t) instead.");
    return false;
}

bool BME280Sensor::init(i2c_master_bus_handle_t bus_handle) {
    if (_initialized) {
        ESP_LOGW(TAG, "Sensor already initialized");
        return true;
    }
    
    if (bus_handle == nullptr) {
        ESP_LOGE(TAG, "Invalid bus handle (null)");
        return false;
    }
    
    _bus_handle = bus_handle;
    
    ESP_LOGI(TAG, "Initializing BME280 sensor");
    
    // Configure device
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BME280_I2C_ADDR,
        .scl_speed_hz = 400000,
    };

    // Create device handle
    esp_err_t ret = i2c_master_bus_add_device(_bus_handle, &dev_cfg, &_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device to I2C bus: %s", esp_err_to_name(ret));
        return false;
    }

    // Check device ID
    uint8_t chip_id;
    ret = readRegister(REG_CHIP_ID, &chip_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID: %s", esp_err_to_name(ret));
        return false;
    }

    if (chip_id != BME280_CHIP_ID) {
        ESP_LOGE(TAG, "Invalid chip ID: 0x%02x, expected 0x%02x", chip_id, BME280_CHIP_ID);
        return false;
    }
    
    // Reset the sensor
    ret = writeRegister(REG_RESET, 0xB6);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset sensor: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Wait for reset to complete
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Read calibration data
    ret = readCalibrationData();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read calibration data: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Set humidity oversampling to x1
    ret = writeRegister(REG_CTRL_HUM, OSRS_X1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set humidity oversampling: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Set temperature and pressure oversampling to x1 and set to normal mode
    uint8_t meas_reg = (OSRS_X1 << 5) | (OSRS_X1 << 2) | MODE_NORMAL;
    ret = writeRegister(REG_CTRL_MEAS, meas_reg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set measurement control: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Set filter coefficient and standby time
    uint8_t config_reg = (FILTER_X4 << 2) | (STANDBY_250_MS << 5);
    ret = writeRegister(REG_CONFIG, config_reg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set config: %s", esp_err_to_name(ret));
        return false;
    }
    
    _initialized = true;
    ESP_LOGI(TAG, "BME280 sensor initialized successfully");
    
    // Initial reading
    poll();
    
    return true;
}

void BME280Sensor::poll() {
    if (!_initialized) {
        ESP_LOGW(TAG, "Sensor not initialized, cannot poll");
        return;
    }
    
    esp_err_t ret = readRawData();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read sensor data: %s", esp_err_to_name(ret));
        return;
    }
    
    // Print sensor readings at INFO level so they're visible in normal operation
    ESP_LOGI(TAG, "Temperature=%.2f°C (%.2f°F), Pressure=%.2fhPa, Humidity=%.2f%%", 
             _temperature, getTemperatureFahrenheit(), _pressure, _humidity);
}

esp_err_t BME280Sensor::readCalibrationData() {
    uint8_t buffer[26];
    
    // Read temperature and pressure calibration data (registers 0x88-0xA1)
    esp_err_t ret = readRegister(REG_CALIB_T1_LSB, buffer, 26);
    if (ret != ESP_OK) {
        return ret;
    }
    
    _calibData.dig_T1 = (buffer[1] << 8) | buffer[0];
    _calibData.dig_T2 = (buffer[3] << 8) | buffer[2];
    _calibData.dig_T3 = (buffer[5] << 8) | buffer[4];
    
    _calibData.dig_P1 = (buffer[7] << 8) | buffer[6];
    _calibData.dig_P2 = (buffer[9] << 8) | buffer[8];
    _calibData.dig_P3 = (buffer[11] << 8) | buffer[10];
    _calibData.dig_P4 = (buffer[13] << 8) | buffer[12];
    _calibData.dig_P5 = (buffer[15] << 8) | buffer[14];
    _calibData.dig_P6 = (buffer[17] << 8) | buffer[16];
    _calibData.dig_P7 = (buffer[19] << 8) | buffer[18];
    _calibData.dig_P8 = (buffer[21] << 8) | buffer[20];
    _calibData.dig_P9 = (buffer[23] << 8) | buffer[22];
    
    _calibData.dig_H1 = buffer[25];
    
    // Read humidity calibration data (registers 0xE1-0xE7)
    uint8_t h_buffer[7];
    ret = readRegister(REG_CALIB_H2_LSB, h_buffer, 7);
    if (ret != ESP_OK) {
        return ret;
    }
    
    _calibData.dig_H2 = (h_buffer[1] << 8) | h_buffer[0];
    _calibData.dig_H3 = h_buffer[2];
    _calibData.dig_H4 = (h_buffer[3] << 4) | (h_buffer[4] & 0x0F);
    _calibData.dig_H5 = (h_buffer[5] << 4) | (h_buffer[4] >> 4);
    _calibData.dig_H6 = (int8_t)h_buffer[6];
    
    ESP_LOGD(TAG, "Calibration data read successfully");
    return ESP_OK;
}

esp_err_t BME280Sensor::readRawData() {
    uint8_t data[8];
    esp_err_t ret = readRegister(REG_PRESS_MSB, data, 8);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Extract pressure (20 bits)
    int32_t raw_pressure = ((int32_t)data[0] << 12) | ((int32_t)data[1] << 4) | ((int32_t)data[2] >> 4);
    
    // Extract temperature (20 bits)
    int32_t raw_temperature = ((int32_t)data[3] << 12) | ((int32_t)data[4] << 4) | ((int32_t)data[5] >> 4);
    
    // Extract humidity (16 bits)
    int32_t raw_humidity = ((int32_t)data[6] << 8) | (int32_t)data[7];
    
    // Apply compensation
    int32_t temp = compensateTemperature(raw_temperature);
    _temperature = (float)temp / 100.0f;
    
    uint32_t press = compensatePressure(raw_pressure);
    _pressure = (float)press / 100.0f; // Convert Pa to hPa
    
    uint32_t hum = compensateHumidity(raw_humidity);
    _humidity = (float)hum / 1024.0f;
    
    return ESP_OK;
}

int32_t BME280Sensor::compensateTemperature(int32_t adc_T) {
    int32_t var1, var2;
    var1 = ((((adc_T >> 3) - ((int32_t)_calibData.dig_T1 << 1))) * ((int32_t)_calibData.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)_calibData.dig_T1)) * ((adc_T >> 4) - ((int32_t)_calibData.dig_T1))) >> 12) * ((int32_t)_calibData.dig_T3)) >> 14;
    
    _t_fine = var1 + var2;
    return (_t_fine * 5 + 128) >> 8;
}

uint32_t BME280Sensor::compensatePressure(int32_t adc_P) {
    int64_t var1, var2, p;
    
    var1 = ((int64_t)_t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)_calibData.dig_P6;
    var2 = var2 + ((var1 * (int64_t)_calibData.dig_P5) << 17);
    var2 = var2 + (((int64_t)_calibData.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)_calibData.dig_P3) >> 8) + ((var1 * (int64_t)_calibData.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)_calibData.dig_P1) >> 33;
    
    if (var1 == 0) {
        return 0; // Avoid exception caused by division by zero
    }
    
    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)_calibData.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)_calibData.dig_P8) * p) >> 19;
    
    p = ((p + var1 + var2) >> 8) + (((int64_t)_calibData.dig_P7) << 4);
    return (uint32_t)p;
}

uint32_t BME280Sensor::compensateHumidity(int32_t adc_H) {
    int32_t v_x1_u32r;
    
    v_x1_u32r = (_t_fine - ((int32_t)76800));
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)_calibData.dig_H4) << 20) - (((int32_t)_calibData.dig_H5) * v_x1_u32r)) +
                   ((int32_t)16384)) >> 15) * (((((((v_x1_u32r * ((int32_t)_calibData.dig_H6)) >> 10) * (((v_x1_u32r *
                   ((int32_t)_calibData.dig_H3)) >> 11) + ((int32_t)32768))) >> 10) + ((int32_t)2097152)) *
                   ((int32_t)_calibData.dig_H2) + 8192) >> 14));
    
    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((int32_t)_calibData.dig_H1)) >> 4));
    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
    
    return (uint32_t)(v_x1_u32r >> 12);
}

float BME280Sensor::getTemperature() const {
    return _temperature;
}

float BME280Sensor::getTemperatureFahrenheit() const {
    return _temperature * 9.0f / 5.0f + 32.0f;
}

float BME280Sensor::getPressure() const {
    return _pressure;
}

float BME280Sensor::getHumidity() const {
    return _humidity;
} 