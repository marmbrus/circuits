#include "amg8833_sensor.h"
#include "i2c_master_ext.h"
#include "esp_log.h"
#include <string.h>
#include "communication.h"
#include "wifi.h"

static const char *TAG = "AMG8833Sensor";

AMG8833Sensor::AMG8833Sensor()
    : I2CSensor(nullptr) {}

AMG8833Sensor::~AMG8833Sensor() {
    if (_tag_collection != nullptr) {
        free_tag_collection(_tag_collection);
        _tag_collection = nullptr;
    }
}

uint8_t AMG8833Sensor::addr() const { return AMG8833_ADDR; }

std::string AMG8833Sensor::name() const { return std::string("Panasonic AMG8833 Grid-EYE"); }

bool AMG8833Sensor::isInitialized() const { return _initialized; }

bool AMG8833Sensor::init() {
    ESP_LOGE(TAG, "Invalid init() without bus handle. Use init(bus_handle).");
    return false;
}

esp_err_t AMG8833Sensor::writeRegister(uint8_t reg, uint8_t value) {
    if (_dev_handle == nullptr) return ESP_ERR_INVALID_STATE;
    return i2c_master_bus_write_uint8(_dev_handle, reg, value);
}

esp_err_t AMG8833Sensor::readRegister(uint8_t reg, uint8_t &value) {
    if (_dev_handle == nullptr) return ESP_ERR_INVALID_STATE;
    return i2c_master_bus_read_uint8(_dev_handle, reg, &value);
}

esp_err_t AMG8833Sensor::readBlock(uint8_t start_reg, uint8_t *buf, size_t len) {
    if (_dev_handle == nullptr || buf == nullptr || len == 0) return ESP_ERR_INVALID_ARG;
    // Write start register then read len bytes
    // Using transmit_receive directly via helper that reads one reg at a time is inefficient;
    // but our helpers are 1-byte; fall back to multiple reads for simplicity and reliability
    for (size_t i = 0; i < len; ++i) {
        esp_err_t err = i2c_master_bus_read_uint8(_dev_handle, (uint8_t)(start_reg + i), &buf[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

float AMG8833Sensor::convertThermistorRaw(uint16_t raw_le) {
    // 12-bit signed, LSB = 0.0625°C; data is little-endian
    int16_t raw = (int16_t)(raw_le & 0x0FFF);
    if (raw & 0x0800) raw -= 0x1000; // sign extend from 12-bit
    return (float)raw * 0.0625f;
}

float AMG8833Sensor::convertPixelRaw(uint16_t raw_le) {
    // 12-bit signed, LSB = 0.25°C; data is little-endian
    int16_t raw = (int16_t)(raw_le & 0x0FFF);
    if (raw & 0x0800) raw -= 0x1000; // sign extend from 12-bit
    return (float)raw * 0.25f;
}

bool AMG8833Sensor::init(i2c_master_bus_handle_t bus_handle) {
    if (_initialized) return true;
    if (bus_handle == nullptr) return false;

    _bus_handle = bus_handle;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = AMG8833_ADDR,
        .scl_speed_hz    = 400000,
        .scl_wait_us     = 0,
        .flags           = 0
    };
    esp_err_t ret = i2c_master_bus_add_device(_bus_handle, &dev_cfg, &_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add AMG8833: %s", esp_err_to_name(ret));
        return false;
    }

    // Initial reset, normal mode, set frame rate to 10fps
    writeRegister(REG_RST, RST_INITIAL_RESET);
    vTaskDelay(pdMS_TO_TICKS(2));
    writeRegister(REG_PCTL, PCTL_NORMAL_MODE);
    writeRegister(REG_FPSC, FPSC_10FPS);

    _tag_collection = create_tag_collection();
    if (_tag_collection) {
        add_tag_to_collection(_tag_collection, "type", "amg8833");
    }

    _initialized = true;
    return true;
}

void AMG8833Sensor::poll() {
    if (!_initialized) return;

    // Read thermistor
    uint8_t tl = 0, th = 0;
    if (readRegister(REG_TTHL, tl) == ESP_OK && readRegister(REG_TTHH, th) == ESP_OK) {
        uint16_t raw = (uint16_t)tl | ((uint16_t)th << 8);
        _thermistor_c = convertThermistorRaw(raw);
    }

    // Read all 64 pixels from 0x80..0xFF (low,high per pixel)
    uint8_t buf[128];
    if (readBlock(REG_PIXEL_BASE, buf, sizeof(buf)) == ESP_OK) {
        for (int i = 0; i < 64; ++i) {
            uint8_t lo = buf[i * 2 + 0];
            uint8_t hi = buf[i * 2 + 1];
            uint16_t raw = (uint16_t)lo | ((uint16_t)hi << 8);
            _pixels_c[i] = convertPixelRaw(raw);
        }

        // Publish tightly packed binary frame: 64 x int16 little-endian in 0.25°C units (signed)
        int16_t frame[64];
        for (int i = 0; i < 64; ++i) {
            // Convert Celsius back to quarter-degree units with rounding
            float q = _pixels_c[i] / 0.25f;
            int v = (int)(q >= 0 ? (q + 0.5f) : (q - 0.5f));
            if (v < -2048) v = -2048; // clamp 12-bit signed range
            if (v >  2047) v =  2047;
            frame[i] = (int16_t)v;
        }
        // Build full topic sensor/<mac>/camera and publish binary frame
        char mac_nosep[13];
        const uint8_t* mac = get_device_mac();
        snprintf(mac_nosep, sizeof(mac_nosep), "%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        char topic[64];
        snprintf(topic, sizeof(topic), "sensor/%s/camera", mac_nosep);
        publish_binary_to_subtopic(topic, (const uint8_t*)frame, sizeof(frame), 0, 0);
    }

    // Optionally report a couple metrics (average temperature)
    float sum = 0.0f;
    for (int i = 0; i < 64; ++i) sum += _pixels_c[i];
    float avg = sum / 64.0f;
    if (_tag_collection) {
        report_metric("grid_eye_temp_avg_c", avg, _tag_collection);
        report_metric("grid_eye_thermistor_c", _thermistor_c, _tag_collection);
    }
}

bool AMG8833Sensor::probe(i2c_master_bus_handle_t bus_handle) {
    if (bus_handle == nullptr) return false;

    // Temporary device
    i2c_master_dev_handle_t temp_dev = nullptr;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = AMG8833_ADDR,
        .scl_speed_hz    = 400000,
        .scl_wait_us     = 0,
        .flags           = 0
    };
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &temp_dev);
    if (err != ESP_OK) return true; // best-effort

    // Read-only signature checks per Panasonic I2C map
    // 1) PCTL must be one of {0x00, 0x10, 0x20, 0x21}
    uint8_t pctl = 0;
    if (i2c_master_bus_read_uint8(temp_dev, REG_PCTL, &pctl) == ESP_OK) {
        if (!(pctl == PCTL_NORMAL_MODE || pctl == PCTL_SLEEP_MODE || pctl == PCTL_STANDBY_60S || pctl == PCTL_STANDBY_10S)) {
            i2c_master_bus_rm_device(temp_dev);
            return false;
        }
    }

    // 2) FPSC only uses bit0; others should be zero
    uint8_t fpsc = 0;
    if (i2c_master_bus_read_uint8(temp_dev, REG_FPSC, &fpsc) == ESP_OK) {
        if ((fpsc & 0xFE) != 0) { // bits 7..1 must be zero
            i2c_master_bus_rm_device(temp_dev);
            return false;
        }
    }

    // 3) INTC only uses bits1:0
    uint8_t intc = 0;
    if (i2c_master_bus_read_uint8(temp_dev, REG_INTC, &intc) == ESP_OK) {
        if ((intc & 0xFC) != 0) { // bits 7..2 must be zero
            i2c_master_bus_rm_device(temp_dev);
            return false;
        }
    }

    // 4) STAT uses bits2:1; others zero
    uint8_t stat = 0;
    if (i2c_master_bus_read_uint8(temp_dev, REG_STAT, &stat) == ESP_OK) {
        if ((stat & 0xF8) != 0) { // bits 7..3 must be zero
            i2c_master_bus_rm_device(temp_dev);
            return false;
        }
    }

    // 5) Pixel high/thermistor high reserved upper nibble should be zero
    uint8_t tthh = 0;
    if (i2c_master_bus_read_uint8(temp_dev, REG_TTHH, &tthh) == ESP_OK) {
        if ((tthh & 0xF0) != 0) { // upper nibble reserved
            i2c_master_bus_rm_device(temp_dev);
            return false;
        }
    }
    uint8_t to1h = 0; // pixel 1 high byte
    (void)i2c_master_bus_read_uint8(temp_dev, 0x81, &to1h);
    if (to1h && ((to1h & 0xF0) != 0)) {
        i2c_master_bus_rm_device(temp_dev);
        return false;
    }

    i2c_master_bus_rm_device(temp_dev);
    // If all read checks we attempted are consistent, accept; otherwise best-effort true when reads failed
    return true;
}

float AMG8833Sensor::getThermistorCelsius() const { return _thermistor_c; }

void AMG8833Sensor::getPixelsCelsius(float out_pixels[64]) const {
    if (!out_pixels) return;
    memcpy(out_pixels, _pixels_c, sizeof(_pixels_c));
}


