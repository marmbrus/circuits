#include "scd4x_sensor.h"
#include "esp_log.h"
#include <string.h>
#include "esp_timer.h"

static const char *TAG = "SCD4xSensor";

SCD4xSensor::SCD4xSensor() :
    I2CSensor(nullptr),
    _co2(0.0f),
    _temperature(0.0f),
    _humidity(0.0f),
    _initialized(false),
    _tag_collection(nullptr) {
    ESP_LOGD(TAG, "SCD4xSensor constructed");
}

SCD4xSensor::~SCD4xSensor() {
    if (_tag_collection != nullptr) {
        free_tag_collection(_tag_collection);
        _tag_collection = nullptr;
    }
    // If initialized, stop measurements
    if (_initialized) {
        sendCommand(CMD_STOP_PERIODIC_MEASUREMENT);
    }
}

uint8_t SCD4xSensor::addr() const {
    return SCD4X_I2C_ADDR;
}

std::string SCD4xSensor::name() const {
    return "Sensirion SCD4x CO2 Sensor";
}

bool SCD4xSensor::isInitialized() const {
    return _initialized;
}

bool SCD4xSensor::init() {
    ESP_LOGE(TAG, "Invalid init() call without bus handle. Use init(i2c_master_bus_handle_t) instead.");
    return false;
}

bool SCD4xSensor::init(i2c_master_bus_handle_t bus_handle) {
    if (_initialized) {
        ESP_LOGW(TAG, "Sensor already initialized");
        return true;
    }

    if (bus_handle == nullptr) {
        ESP_LOGE(TAG, "Invalid bus handle (null)");
        return false;
    }

    _bus_handle = bus_handle;
    ESP_LOGI(TAG, "Initializing SCD4x sensor");

    // Use a slower I2C speed & allow clock stretching for reliable comms
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = SCD4X_I2C_ADDR,
        .scl_speed_hz    = 50000,  // Lower speed than the default 100kHz
        .scl_wait_us     = 20,     // Allow clock stretching
        .flags           = 0
    };

    esp_err_t ret = i2c_master_bus_add_device(_bus_handle, &dev_cfg, &_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SCD4x to I2C bus: %s", esp_err_to_name(ret));
        return false;
    }

    // Give the sensor time to wake up
    vTaskDelay(pdMS_TO_TICKS(100));

    // Stop any previous measurements (just in case)
    ret = sendCommand(CMD_STOP_PERIODIC_MEASUREMENT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop existing measurement: %s", esp_err_to_name(ret));
    }

    // According to the datasheet, wait 500ms after STOP before sending more commands
    vTaskDelay(pdMS_TO_TICKS(500));

    // Replace "CMD_RESET (0x94A2)" with "CMD_REINIT (0x3646)":
    const uint16_t CMD_REINIT = 0x3646;
    ret = sendCommand(CMD_REINIT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reInit (instead of soft reset) SCD4x: %s", esp_err_to_name(ret));
        return false;
    }

    // According to the datasheet, reInit requires a short delay before next command
    vTaskDelay(pdMS_TO_TICKS(20));

    // 3) Start periodic measuring
    ret = sendCommand(CMD_START_PERIODIC_MEASUREMENT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start SCD4x measurement: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "SCD4x periodic measurement started");

    // Create TagCollection (as before)
    _tag_collection = create_tag_collection();
    if (_tag_collection == nullptr) {
        ESP_LOGE(TAG, "Failed to create tag collection");
        return false;
    }

    // Add SCD4x-specific tags
    esp_err_t err_type = add_tag_to_collection(_tag_collection, "type", "scd4x");
    esp_err_t err_name = add_tag_to_collection(_tag_collection, "name", "co2");
    if (err_type != ESP_OK || err_name != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add sensor tags");
        free_tag_collection(_tag_collection);
        _tag_collection = nullptr;
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG, "SCD4x sensor initialized successfully");
    _init_time_ms = (unsigned long long)(esp_timer_get_time() / 1000);

    return true;
}

void SCD4xSensor::poll() {
    if (!_initialized) {
        ESP_LOGW(TAG, "SCD4x not initialized, cannot poll");
        return;
    }

    // Check data-ready status first and only proceed when ready.
    // Warn only after 3 consecutive not-ready polls to avoid log spam.
    {
        static int s_not_ready_count = 0;
        const uint16_t CMD_GET_DATA_READY_STATUS = 0xE4B8;
        if (sendCommand(CMD_GET_DATA_READY_STATUS) == ESP_OK) {
            // Minimum wait before reading response
            vTaskDelay(pdMS_TO_TICKS(2));
            uint8_t status_rx[3] = {0};
            if (i2c_master_receive(_dev_handle, status_rx, sizeof(status_rx), 100) == ESP_OK) {
                // Validate CRC (per SCD4x protocol)
                uint8_t crc = calculateCRC(status_rx, 2);
                if (crc == status_rx[2]) {
                    uint16_t status = (static_cast<uint16_t>(status_rx[0]) << 8) | status_rx[1];
                    bool ready = (status & 0x07FF) != 0; // any of 11 LSBs set => data ready
                    if (!ready) {
                        s_not_ready_count++;
                        if (s_not_ready_count >= 3) {
                            ESP_LOGW(TAG, "SCD4x data not ready for 3 consecutive polls");
                            s_not_ready_count = 0; // reset after reporting
                        }
                        return; // Skip this poll to avoid impacting the bus
                    }
                    // Ready: reset counter and proceed
                    s_not_ready_count = 0;
                }
            }
        }
    }

    esp_err_t ret = readMeasurement();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read SCD4x data: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "CO2=%.1f ppm, Temperature=%.2f°C (%.2f°F), Humidity=%.2f%%",
             _co2, _temperature, getTemperatureFahrenheit(), _humidity);

    // Report metrics
    static const char* METRIC_CO2          = "co2";
    static const char* METRIC_TEMPERATURE  = "temperature_f";
    static const char* METRIC_HUMIDITY     = "humidity";

    // Respect shared warm-up: skip reporting for first I2C_SENSOR_WARMUP_MS
    if (is_warming_up()) {
        return;
    }
    report_metric(METRIC_CO2, _co2, _tag_collection);
    report_metric(METRIC_TEMPERATURE, getTemperatureFahrenheit(), _tag_collection);
    report_metric(METRIC_HUMIDITY, _humidity, _tag_collection);
}

esp_err_t SCD4xSensor::sendCommand(uint16_t command) {
    uint8_t cmd_bytes[2] = {
        static_cast<uint8_t>(command >> 8),
        static_cast<uint8_t>(command & 0xFF)
    };

    for (int attempt = 1; attempt <= 3; attempt++) {
        esp_err_t ret = i2c_master_transmit(_dev_handle, cmd_bytes, 2, 500); // 500ms timeout
        if (ret == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "SCD4x command 0x%04X failed on attempt %d: %s",
                 command, attempt, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    ESP_LOGE(TAG, "Sending SCD4x command 0x%04X failed after 3 attempts", command);
    return ESP_ERR_TIMEOUT;
}

esp_err_t SCD4xSensor::readMeasurement() {
    // Send read command first
    esp_err_t ret = sendCommand(CMD_READ_MEASUREMENT);
    if (ret != ESP_OK) {
        return ret;
    }

    // SCD4x docs recommend at least 1ms after command; wait a bit longer
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t data[9] = {0};
    for (int attempt = 1; attempt <= 3; attempt++) {
        ret = i2c_master_receive(_dev_handle, data, sizeof(data), 500); // up to 500ms
        if (ret == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "SCD4x read failed on attempt %d: %s", attempt, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to receive SCD4x data after 3 attempts");
        return ret;
    }

    // Debug raw data
    ESP_LOGD(TAG, "SCD4x raw bytes:");
    for (int i = 0; i < 9; i++) {
        ESP_LOGD(TAG, "  data[%d] = 0x%02X", i, data[i]);
    }

    // Validate CRCs
    for (int i = 0; i < 9; i += 3) {
        uint8_t crc = calculateCRC(&data[i], 2);
        if (crc != data[i+2]) {
            ESP_LOGE(TAG, "CRC mismatch at chunk %d: calculated=0x%02X, got=0x%02X",
                     i/3, crc, data[i+2]);
            return ESP_ERR_INVALID_CRC;
        }
    }

    // Parse CO2
    uint16_t co2_raw = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    _co2 = static_cast<float>(co2_raw);

    // Parse temperature
    uint16_t temp_raw = (static_cast<uint16_t>(data[3]) << 8) | data[4];
    _temperature = -45.0f + 175.0f * (static_cast<float>(temp_raw) / 65536.0f);

    // Parse humidity
    uint16_t hum_raw = (static_cast<uint16_t>(data[6]) << 8) | data[7];
    _humidity = 100.0f * (static_cast<float>(hum_raw) / 65536.0f);

    ESP_LOGD(TAG, "SCD4x: CO2=%.0f ppm, T=%.2f°C, RH=%.2f%%",
             _co2, _temperature, _humidity);
    return ESP_OK;
}

uint8_t SCD4xSensor::calculateCRC(const uint8_t* data, size_t length) const {
    // CRC-8 calculation with polynomial x^8 + x^5 + x^4 + 1 = 0x31 (SCD4x standard)
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

float SCD4xSensor::getCO2() const {
    return _co2;
}

float SCD4xSensor::getTemperature() const {
    return _temperature;
}

float SCD4xSensor::getHumidity() const {
    return _humidity;
}

float SCD4xSensor::getTemperatureFahrenheit() const {
    return (_temperature * 9.0f / 5.0f) + 32.0f;
}