#include "sen55_sensor.h"
#include "esp_log.h"
#include <string.h>
#include "esp_timer.h"
#include <math.h>

static const char *TAG = "SEN55Sensor";

SEN55Sensor::SEN55Sensor() :
    I2CSensor(nullptr),
    _pm1(0.0f),
    _pm2_5(0.0f),
    _pm4(0.0f),
    _pm10(0.0f),
    _voc(0.0f),
    _nox(0.0f),
    _temperature(25.0f),  // Start with a reasonable room temperature default
    _humidity(50.0f),     // Start with a reasonable humidity default
    _initialized(false),
    _tag_collection(nullptr),
    _startup_readings_count(0) {
    ESP_LOGD(TAG, "SEN55Sensor constructed");
}

SEN55Sensor::~SEN55Sensor() {
    if (_tag_collection != nullptr) {
        free_tag_collection(_tag_collection);
        _tag_collection = nullptr;
    }

    // Stop measurement if initialized
    if (_initialized) {
        sendCommand(CMD_STOP_MEASUREMENT);
    }
}

uint8_t SEN55Sensor::addr() const {
    return SEN55_I2C_ADDR;
}

std::string SEN55Sensor::name() const {
    return "Sensirion SEN55 Environmental Sensor";
}

bool SEN55Sensor::isInitialized() const {
    return _initialized;
}

bool SEN55Sensor::init() {
    ESP_LOGE(TAG, "Invalid init() call without bus handle. Use init(i2c_master_bus_handle_t) instead.");
    return false;
}

bool SEN55Sensor::init(i2c_master_bus_handle_t bus_handle) {
    if (_initialized) {
        ESP_LOGW(TAG, "Sensor already initialized");
        return true;
    }

    if (bus_handle == nullptr) {
        ESP_LOGE(TAG, "Invalid bus handle (null)");
        return false;
    }

    _bus_handle = bus_handle;

    ESP_LOGI(TAG, "Initializing SEN55 sensor");

    // Configure device
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SEN55_I2C_ADDR,
        .scl_speed_hz = 100000, // 100 kHz is the standard for SEN55
        .scl_wait_us = 0,
        .flags = 0
    };

    // Create device handle
    esp_err_t ret = i2c_master_bus_add_device(_bus_handle, &dev_cfg, &_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device to I2C bus: %s", esp_err_to_name(ret));
        return false;
    }

    // Reset the sensor first (idle mode)
    ret = sendCommand(CMD_RESET);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset sensor: %s", esp_err_to_name(ret));
        return false;
    }

    // Wait for reset to complete - according to Sensirion docs this needs at least 100ms
    vTaskDelay(pdMS_TO_TICKS(200));

    // While in Idle, read identity information to avoid NACKs in Measurement-Mode on some firmwares
    {
        char product_name[33] = {0};
        char serial_number[33] = {0};
        uint8_t fw_ver = 0;
        if (readStringFromCommand(CMD_READ_PRODUCT_NAME, product_name, sizeof(product_name)) == ESP_OK) {
            ESP_LOGI(TAG, "Product: %s", product_name);
        }
        if (readStringFromCommand(CMD_READ_SERIAL_NUMBER, serial_number, sizeof(serial_number)) == ESP_OK) {
            ESP_LOGI(TAG, "Serial: %s", serial_number);
        }
        if (readFirmwareVersion(fw_ver) == ESP_OK) {
            ESP_LOGI(TAG, "Firmware version: %u", (unsigned)fw_ver);
        }
    }

    // Start measurement - based on sen5x_start_measurement() in the Sensirion embedded library
    ret = sendCommand(CMD_START_MEASUREMENT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start measurement: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "SEN55 measurement started");

    // Create and populate tag collection
    _tag_collection = create_tag_collection();
    if (_tag_collection == nullptr) {
        ESP_LOGE(TAG, "Failed to create tag collection");
        return false;
    }

    // Add sensor-specific tags
    esp_err_t err_type = add_tag_to_collection(_tag_collection, "type", "sen55");
    esp_err_t err_name = add_tag_to_collection(_tag_collection, "name", "environment");

    if (err_type != ESP_OK || err_name != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add tags to collection");
        free_tag_collection(_tag_collection);
        _tag_collection = nullptr;
        return false;
    }

    _initialized = true;
    ESP_LOGI(TAG, "SEN55 sensor initialized successfully");

    _init_time_ms = (unsigned long long)(esp_timer_get_time() / 1000);

    return true;
}

void SEN55Sensor::poll() {
    if (!_initialized) {
        ESP_LOGW(TAG, "Sensor not initialized, cannot poll");
        return;
    }

    // Respect data-ready flag to avoid reading too fast
    static int s_dr_miss_count = 0;
    if (!dataReady()) {
        s_dr_miss_count++;
        if ((s_dr_miss_count % 20) == 0) {
            ESP_LOGW(TAG, "SEN55 data-not-ready for %d polls (~%ds)", s_dr_miss_count, s_dr_miss_count);
        }
        return;
    } else {
        s_dr_miss_count = 0;
    }

    esp_err_t ret = readMeasurement();
    if (ret != ESP_OK) {
        static int s_not_ready_count = 0;
        s_not_ready_count++;
        if (s_not_ready_count >= 3) {
            ESP_LOGW(TAG, "SEN55 not ready or read failed x3: %s", esp_err_to_name(ret));
            s_not_ready_count = 0;
        }
        return;
    }

    // Increment the startup readings counter
    if (_startup_readings_count < 5) {
        _startup_readings_count++;
    }

    // Print sensor readings at INFO level, including Fahrenheit
    ESP_LOGI(TAG, "PM1.0=%.1f μg/m³, PM2.5=%.1f μg/m³, PM4.0=%.1f μg/m³, PM10=%.1f μg/m³",
             _pm1, _pm2_5, _pm4, _pm10);
    ESP_LOGI(TAG, "VOC=%.1f, NOx=%.1f, Temperature=%.2f°C (%.2f°F), Humidity=%.2f%%",
             _voc, _nox, _temperature, getTemperatureFahrenheit(), _humidity);

    // Report metrics
    static const char* METRIC_PM1 = "pm1";
    static const char* METRIC_PM2_5 = "pm2_5";
    static const char* METRIC_PM4 = "pm4";
    static const char* METRIC_PM10 = "pm10";
    static const char* METRIC_VOC = "voc";
    static const char* METRIC_NOX = "nox";
    static const char* METRIC_TEMPERATURE = "temperature_f"; // Changed to report Fahrenheit
    static const char* METRIC_HUMIDITY = "humidity";

    report_metric(METRIC_PM1, _pm1, _tag_collection);
    report_metric(METRIC_PM2_5, _pm2_5, _tag_collection);
    report_metric(METRIC_PM4, _pm4, _tag_collection);
    report_metric(METRIC_PM10, _pm10, _tag_collection);
    report_metric(METRIC_VOC, _voc, _tag_collection);
    report_metric(METRIC_NOX, _nox, _tag_collection);

    // Read and log device status if any error/warning is present
    uint32_t status = 0;
    if (readDeviceStatus(status) == ESP_OK) {
        if (status != 0) {
            logDeviceStatus(status);
        }
    }

    // Respect shared warm-up: skip reporting RH/T for first I2C_SENSOR_WARMUP_MS
    if (!is_warming_up()) {
        report_metric(METRIC_TEMPERATURE, getTemperatureFahrenheit(), _tag_collection); // Report in Fahrenheit
        report_metric(METRIC_HUMIDITY, _humidity, _tag_collection);
    }
}

esp_err_t SEN55Sensor::sendCommand(uint16_t command) {
    uint8_t cmd_bytes[2] = {
        (uint8_t)(command >> 8),    // MSB first (big-endian)
        (uint8_t)(command & 0xFF)   // LSB second
    };

    esp_err_t ret = i2c_master_transmit(_dev_handle, cmd_bytes, 2, 100); // 100ms timeout
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send command 0x%04x: %s", command, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t SEN55Sensor::readStringFromCommand(uint16_t command, char* out, size_t max_len) {
    if (!out || max_len == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = sendCommand(command);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(20));
    // Up to 32 ASCII chars, with CRC after every 2 bytes. We'll read 48 bytes payload.
    uint8_t buf[48] = {0};
    ret = i2c_master_receive(_dev_handle, buf, sizeof(buf), 100);
    if (ret != ESP_OK) return ret;
    size_t out_idx = 0;
    for (int i = 0; i < 48; i += 3) {
        // validate CRC
        uint8_t crc = calculateCRC(&buf[i], 2);
        if (crc != buf[i+2]) break;
        if (out_idx + 2 < max_len) {
            out[out_idx++] = (char)buf[i];
            out[out_idx++] = (char)buf[i+1];
        }
    }
    if (out_idx >= max_len) out_idx = max_len - 1;
    out[out_idx] = '\0';
    return ESP_OK;
}

esp_err_t SEN55Sensor::readFirmwareVersion(uint8_t &fw_version) {
    esp_err_t ret = sendCommand(CMD_READ_FIRMWARE_VERSION);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t data[3] = {0};
    ret = i2c_master_receive(_dev_handle, data, sizeof(data), 100);
    if (ret != ESP_OK) return ret;
    uint8_t crc = calculateCRC(data, 2);
    if (crc != data[2]) return ESP_ERR_INVALID_CRC;
    fw_version = data[0];
    return ESP_OK;
}

esp_err_t SEN55Sensor::readDeviceStatus(uint32_t &status) {
    esp_err_t ret = sendCommand(CMD_READ_DEVICE_STATUS);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t data[6] = {0};
    ret = i2c_master_receive(_dev_handle, data, sizeof(data), 100);
    if (ret != ESP_OK) return ret;
    // validate two CRCs
    if (calculateCRC(&data[0], 2) != data[2]) return ESP_ERR_INVALID_CRC;
    if (calculateCRC(&data[3], 2) != data[5]) return ESP_ERR_INVALID_CRC;
    uint16_t msw = ((uint16_t)data[0] << 8) | data[1];
    uint16_t lsw = ((uint16_t)data[3] << 8) | data[4];
    status = ((uint32_t)msw << 16) | lsw;
    return ESP_OK;
}

void SEN55Sensor::logDeviceStatus(uint32_t status) {
    // Bits per datasheet section 5.4
    bool speed = (status >> 21) & 0x1;
    bool fan_cleaning = (status >> 19) & 0x1;
    bool gas_err = (status >> 7) & 0x1;
    bool rht_comm_err = (status >> 6) & 0x1;
    bool laser_fail = (status >> 5) & 0x1;
    bool fan_fail = (status >> 4) & 0x1;
    ESP_LOGW(TAG, "Device Status=0x%08lx SPEED=%d FAN_CLEAN=%d GAS_ERR=%d RHT_ERR=%d LASER_FAIL=%d FAN_FAIL=%d",
             (unsigned long)status, speed, fan_cleaning, gas_err, rht_comm_err, laser_fail, fan_fail);
}

bool SEN55Sensor::dataReady() {
    // Read Data-Ready Flag (0x0202) -> 3 bytes response (2 data + CRC)
    if (sendCommand(CMD_READ_DATA_READY) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t data[3] = {0};
    if (i2c_master_receive(_dev_handle, data, sizeof(data), 100) != ESP_OK) return false;
    if (calculateCRC(data, 2) != data[2]) return false;
    // data[1] == 0x01 when new measurements are ready
    return data[1] == 0x01;
}

esp_err_t SEN55Sensor::sendCommandWithArgs(uint16_t command, const uint8_t* args, size_t args_len) {
    // Create buffer for command (2 bytes) + args + CRC bytes
    size_t buffer_size = 2 + args_len + (args_len / 2); // Each 2 bytes of args needs 1 CRC byte
    uint8_t* buffer = (uint8_t*)malloc(buffer_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for command with args");
        return ESP_ERR_NO_MEM;
    }

    // Add command (big-endian)
    buffer[0] = (uint8_t)(command >> 8);
    buffer[1] = (uint8_t)(command & 0xFF);

    // Add args with CRC
    size_t buf_idx = 2;
    for (size_t i = 0; i < args_len; i += 2) {
        // Copy 2 bytes of args
        buffer[buf_idx++] = args[i];
        buffer[buf_idx++] = args[i+1];

        // Calculate and add CRC
        buffer[buf_idx++] = calculateCRC(&args[i], 2);
    }

    // Send the command with args
    esp_err_t ret = i2c_master_transmit(_dev_handle, buffer, buffer_size, 100); // 100ms timeout

    // Free the buffer
    free(buffer);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send command 0x%04x with args: %s", command, esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t SEN55Sensor::readMeasurement() {
    // Step 1: Send read measurement command (0x03C4)
    esp_err_t ret = sendCommand(CMD_READ_MEASUREMENT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send read measurement command: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 2: Wait for measurement data to be ready
    vTaskDelay(pdMS_TO_TICKS(20));

    // Step 3: Read measurement data (24 bytes total)
    uint8_t data[24];
    ret = i2c_master_receive(_dev_handle, data, sizeof(data), 100); // 100ms timeout
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read measurement data: %s", esp_err_to_name(ret));
        return ret;
    }

    // Debug raw data
    ESP_LOGD(TAG, "SEN55 raw data (hex):");
    for (int i = 0; i < 24; i += 3) {
        ESP_LOGD(TAG, "  Value %d: 0x%02x 0x%02x (CRC: 0x%02x)", i/3, data[i], data[i+1], data[i+2]);
    }

    // Step 4: Verify CRC for each 2-byte data chunk
    for (int i = 0; i < 24; i += 3) {
        uint8_t crc = calculateCRC(&data[i], 2);
        if (crc != data[i+2]) {
            ESP_LOGE(TAG, "CRC error at bytes %d-%d: calculated 0x%02x, received 0x%02x",
                    i, i+1, crc, data[i+2]);
            return ESP_ERR_INVALID_CRC;
        }
    }

    // Step 5: Parse measurement data according to Sensirion embedded-i2c-sen5x library
    // Reference: sen5x_i2c.c in the Sensirion GitHub repo

    // Mass Concentration PMx [μg/m³]
    uint16_t pm1p0  = (uint16_t)((data[0] << 8) | data[1]);
    uint16_t pm2p5  = (uint16_t)((data[3] << 8) | data[4]);
    uint16_t pm4p0  = (uint16_t)((data[6] << 8) | data[7]);
    uint16_t pm10p0 = (uint16_t)((data[9] << 8) | data[10]);
    _pm1   = pm1p0 / 10.0f;
    _pm2_5 = pm2p5 / 10.0f;
    _pm4   = pm4p0 / 10.0f;
    _pm10  = pm10p0 / 10.0f;
    if ((pm1p0 == 0xFFFF) || (pm2p5 == 0xFFFF) || (pm4p0 == 0xFFFF) || (pm10p0 == 0xFFFF)) {
        // Log once per poll if module reports 'unknown' PM values, but do not suppress reporting
        uint32_t status = 0;
        if (readDeviceStatus(status) == ESP_OK) {
            bool speed = (status >> 21) & 0x1;
            bool fan_clean = (status >> 19) & 0x1;
            bool laser_fail = (status >> 5) & 0x1;
            bool fan_fail = (status >> 4) & 0x1;
            ESP_LOGW(TAG, "PM returned 0xFFFF; status=0x%08lx speed=%d fan_clean=%d laser_fail=%d fan_fail=%d",
                     (unsigned long)status, speed, fan_clean, laser_fail, fan_fail);
        } else {
            ESP_LOGW(TAG, "PM returned 0xFFFF; status read failed");
        }
    }

    // Humidity [%RH] (bytes 12..13), scale 100 per datasheet
    int16_t humidityRaw = (int16_t)((data[12] << 8) | data[13]);
    if (humidityRaw == 0x7FFF) {
        if (_startup_readings_count >= 5) {
            ESP_LOGW(TAG, "Humidity not available (raw=0x7FFF)");
        } else {
            ESP_LOGD(TAG, "Humidity not available during startup (raw=0x7FFF)");
        }
    } else {
        // According to Sensirion SEN5x, humidity is reported as raw/100 (percent)
        _humidity = humidityRaw / 100.0f;
    }

    // Temperature [°C] (bytes 15..16), scale 200 per datasheet
    int16_t tempRaw = (int16_t)((data[15] << 8) | data[16]);
    if (tempRaw == 0x7FFF) {
        if (_startup_readings_count >= 5) {
            ESP_LOGW(TAG, "Temperature not available (raw=0x7FFF)");
        } else {
            ESP_LOGD(TAG, "Temperature not available during startup (raw=0x7FFF)");
        }
    } else {
        _temperature = tempRaw / 200.0f;
        ESP_LOGD(TAG, "Temperature calculation: %d / 200 = %.2f°C",
                 tempRaw, _temperature);
    }

    // VOC Index
    int16_t vocIndex = (int16_t)((data[18] << 8) | data[19]);
    if (vocIndex == 0x7FFF) {
        if (_startup_readings_count >= 5) {
            ESP_LOGW(TAG, "VOC Index not available (raw=0x7FFF)");
        } else {
            ESP_LOGD(TAG, "VOC Index not available during startup (raw=0x7FFF)");
        }
    } else {
        _voc = vocIndex / 10.0f;
    }

    // NOx Index
    int16_t noxIndex = (int16_t)((data[21] << 8) | data[22]);
    if (noxIndex == 0x7FFF) {
        if (_startup_readings_count >= 5) {
            ESP_LOGW(TAG, "NOx Index not available (raw=0x7FFF)");
        } else {
            ESP_LOGD(TAG, "NOx Index not available during startup (raw=0x7FFF)");
        }
    } else {
        _nox = noxIndex / 10.0f;
    }

    // Log parsed values
    ESP_LOGD(TAG, "SEN55 parsed values:");
    ESP_LOGD(TAG, "  PM1.0: %u/10 = %.1f μg/m³", pm1p0, _pm1);
    ESP_LOGD(TAG, "  PM2.5: %u/10 = %.1f μg/m³", pm2p5, _pm2_5);
    ESP_LOGD(TAG, "  PM4.0: %u/10 = %.1f μg/m³", pm4p0, _pm4);
    ESP_LOGD(TAG, "  PM10.0: %u/10 = %.1f μg/m³", pm10p0, _pm10);
    ESP_LOGD(TAG, "  Temperature raw: 0x%04x (%d), value: %.2f°C",
             (uint16_t)tempRaw, tempRaw, _temperature);
    ESP_LOGD(TAG, "  Humidity raw: 0x%04x (%d), value: %.2f%%",
             (uint16_t)humidityRaw, humidityRaw, _humidity);
    ESP_LOGD(TAG, "  VOC Index: 0x%04x (%d), value: %.1f",
             (uint16_t)vocIndex, vocIndex, _voc);
    ESP_LOGD(TAG, "  NOx Index: 0x%04x (%d), value: %.1f",
             (uint16_t)noxIndex, noxIndex, _nox);

    return ESP_OK;
}

uint8_t SEN55Sensor::calculateCRC(const uint8_t* data, size_t length) const {
    // CRC-8 calculation with polynomial x^8 + x^5 + x^4 + 1 = 0x31
    uint8_t crc = 0xFF; // Initial value

    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc = (crc << 1);
            }
        }
    }

    return crc;
}

float SEN55Sensor::getPM1() const {
    return _pm1;
}

float SEN55Sensor::getPM2_5() const {
    return _pm2_5;
}

float SEN55Sensor::getPM4() const {
    return _pm4;
}

float SEN55Sensor::getPM10() const {
    return _pm10;
}

float SEN55Sensor::getVOC() const {
    return _voc;
}

float SEN55Sensor::getNOx() const {
    return _nox;
}

float SEN55Sensor::getTemperature() const {
    return _temperature;
}

float SEN55Sensor::getTemperatureFahrenheit() const {
    return (_temperature * 9.0f / 5.0f) + 32.0f;
}

float SEN55Sensor::getHumidity() const {
    return _humidity;
}