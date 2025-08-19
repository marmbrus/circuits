#include "opt3001_sensor.h"
#include "i2c_master_ext.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "OPT3001Sensor";

OPT3001Sensor::OPT3001Sensor()
    : I2CSensor(nullptr),
      _lux(0.0f),
      _initialized(false),
      _tag_collection(nullptr) {
}

OPT3001Sensor::~OPT3001Sensor() {
    if (_tag_collection != nullptr) {
        free_tag_collection(_tag_collection);
        _tag_collection = nullptr;
    }
}

uint8_t OPT3001Sensor::addr() const {
    return OPT3001_I2C_ADDR;
}

std::string OPT3001Sensor::name() const {
    return "TI OPT3001 Ambient Light";
}

bool OPT3001Sensor::isInitialized() const {
    return _initialized;
}

bool OPT3001Sensor::init() {
    ESP_LOGE(TAG, "Invalid init() without bus handle. Use init(bus_handle).");
    return false;
}

esp_err_t OPT3001Sensor::writeRegister(uint8_t reg, uint16_t value_be) {
    if (_dev_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    // Use helper: write 1-byte reg + 16-bit little-endian? OPT3001 expects big-endian for data bytes.
    // i2c_master_ext write_uint16 sends little-endian; so compose buffer manually with raw transmit.
    uint8_t tx[3] = { reg, static_cast<uint8_t>(value_be >> 8), static_cast<uint8_t>(value_be & 0xFF) };
    return i2c_master_transmit(_dev_handle, tx, sizeof(tx), I2C_XFR_TIMEOUT_MS);
}

esp_err_t OPT3001Sensor::readRegister(uint8_t reg, uint16_t &value_be) {
    if (_dev_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t rx[2] = {0, 0};
    esp_err_t ret = i2c_master_transmit_receive(_dev_handle, &reg, 1, rx, 2, I2C_XFR_TIMEOUT_MS);
    if (ret != ESP_OK) {
        return ret;
    }
    value_be = (static_cast<uint16_t>(rx[0]) << 8) | rx[1];
    return ESP_OK;
}

esp_err_t OPT3001Sensor::configureContinuousAutoRange() {
    // Config register bits per datasheet:
    // RN(15:12)=1100 (automatic full-scale)
    // CT(11)=1 (800ms conversion)
    // M(10:9)=10 (continuous conversions)
    // Latch(4)=0, Pol(3)=0, Mask Exponent(2)=0, Fault count(1:0)=00
    const uint16_t cfg = 0xCC00; // 1100 1100 0000 0000
    esp_err_t ret = writeRegister(REG_CONFIG, cfg);
    if (ret == ESP_OK) {
        uint16_t readback = 0;
        if (readRegister(REG_CONFIG, readback) == ESP_OK) {
            ESP_LOGI(TAG, "CONFIG written=0x%04X readback=0x%04X", cfg, readback);
        }
    }
    return ret;
}

bool OPT3001Sensor::init(i2c_master_bus_handle_t bus_handle) {
    if (_initialized) {
        return true;
    }
    if (bus_handle == nullptr) {
        return false;
    }
    _bus_handle = bus_handle;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = OPT3001_I2C_ADDR,
        .scl_speed_hz    = 100000,
    };
    esp_err_t ret = i2c_master_bus_add_device(_bus_handle, &dev_cfg, &_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add OPT3001: %s", esp_err_to_name(ret));
        return false;
    }

    // Verify IDs
    uint16_t man_id = 0;
    ret = readRegister(REG_MANUFACTURER_ID, man_id);
    if (ret != ESP_OK || man_id != MANUFACTURER_ID_TI) {
        ESP_LOGE(TAG, "Manufacturer ID mismatch: 0x%04X", man_id);
        return false;
    }
    uint16_t dev_id = 0;
    ret = readRegister(REG_DEVICE_ID, dev_id);
    if (ret != ESP_OK || dev_id != DEVICE_ID_OPT3001) {
        ESP_LOGE(TAG, "Device ID mismatch: 0x%04X", dev_id);
        return false;
    }

    // Configure continuous mode with auto range
    ret = configureContinuousAutoRange();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure OPT3001: %s", esp_err_to_name(ret));
        return false;
    }

    _tag_collection = create_tag_collection();
    if (_tag_collection == nullptr) {
        ESP_LOGE(TAG, "Failed to create tag collection");
        return false;
    }
    add_tag_to_collection(_tag_collection, "type", "opt3001");
    add_tag_to_collection(_tag_collection, "name", "lux");

    _initialized = true;
    // Give first conversion time (800ms)
    vTaskDelay(pdMS_TO_TICKS(800));
    poll();
    return true;
}

void OPT3001Sensor::poll() {
    if (!_initialized) {
        return;
    }

    // Check conversion ready flag before reading result
    uint16_t cfg_rb = 0;
    if (readRegister(REG_CONFIG, cfg_rb) == ESP_OK) {
        bool conversion_ready = (cfg_rb & 0x0080) != 0; // CRF bit
        if (!conversion_ready) {
            ESP_LOGD(TAG, "Conversion not ready, CFG=0x%04X", cfg_rb);
            return;
        }
    }

    uint16_t raw = 0;
    esp_err_t ret = readRegister(REG_RESULT, raw);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read result: %s", esp_err_to_name(ret));
        return;
    }

    uint16_t exponent = (raw >> 12) & 0x0F;
    uint16_t mantissa = raw & 0x0FFF;
    _lux = static_cast<float>(mantissa) * (0.01f * static_cast<float>(1 << exponent));

    ESP_LOGI(TAG, "Lux=%.2f (E=%u M=0x%03X)", _lux, exponent, mantissa);
    report_metric("lux", _lux, _tag_collection);
}

float OPT3001Sensor::getLux() const {
    return _lux;
}


