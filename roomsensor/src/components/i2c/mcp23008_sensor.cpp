#include "mcp23008_sensor.h"
#include "i2c_master_ext.h"
#include "esp_log.h"

static const char *TAG = "MCP23008Sensor";

MCP23008Sensor::MCP23008Sensor(uint8_t i2c_address)
    : I2CSensor(nullptr),
      _i2c_addr(i2c_address),
      _initialized(false),
      _level(0.0f),
      _tag_collection(nullptr) {}

MCP23008Sensor::~MCP23008Sensor() {
    if (_tag_collection != nullptr) {
        free_tag_collection(_tag_collection);
        _tag_collection = nullptr;
    }
}

uint8_t MCP23008Sensor::addr() const {
    return _i2c_addr;
}

std::string MCP23008Sensor::name() const {
    char buf[32];
    snprintf(buf, sizeof(buf), "MCP23008@0x%02X", _i2c_addr);
    return std::string(buf);
}

bool MCP23008Sensor::isInitialized() const {
    return _initialized;
}

bool MCP23008Sensor::init() {
    ESP_LOGE(TAG, "Invalid init() without bus handle. Use init(bus_handle).");
    return false;
}

esp_err_t MCP23008Sensor::writeRegister(uint8_t reg, uint8_t value) {
    if (_dev_handle == nullptr) return ESP_ERR_INVALID_STATE;
    return i2c_master_bus_write_uint8(_dev_handle, reg, value);
}

esp_err_t MCP23008Sensor::readRegister(uint8_t reg, uint8_t &value) {
    if (_dev_handle == nullptr) return ESP_ERR_INVALID_STATE;
    return i2c_master_bus_read_uint8(_dev_handle, reg, &value);
}

void MCP23008Sensor::configureGpio0AsInput() {
    // Set GPIO0 as input (bit0=1), leave others unchanged
    uint8_t iodir = 0xFF; // default reset state is all inputs; read-modify-write for safety
    if (readRegister(REG_IODIR, iodir) != ESP_OK) {
        iodir = 0xFF;
    }
    iodir |= 0x01;
    writeRegister(REG_IODIR, iodir);
}

bool MCP23008Sensor::init(i2c_master_bus_handle_t bus_handle) {
    if (_initialized) return true;
    if (bus_handle == nullptr) return false;

    _bus_handle = bus_handle;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = _i2c_addr,
        .scl_speed_hz    = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(_bus_handle, &dev_cfg, &_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add MCP23008@0x%02X: %s", _i2c_addr, esp_err_to_name(ret));
        return false;
    }

    // Basic configuration: ensure BANK=0, SEQOP=0 in IOCON (use defaults)
    // Configure GPIO0 as input with pull-up
    configureGpio0AsInput();

    _tag_collection = create_tag_collection();
    if (_tag_collection == nullptr) {
        ESP_LOGE(TAG, "Failed to create tag collection");
        return false;
    }
    add_tag_to_collection(_tag_collection, "type", "mcp23008");
    char addr_buf[8]; snprintf(addr_buf, sizeof(addr_buf), "0x%02X", _i2c_addr);
    add_tag_to_collection(_tag_collection, "addr", addr_buf);
    add_tag_to_collection(_tag_collection, "name", "gpio0");

    _initialized = true;
    poll();
    return true;
}

void MCP23008Sensor::poll() {
    if (!_initialized) return;

    uint8_t gpio = 0;
    esp_err_t ret = readRegister(REG_GPIO, gpio);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read GPIO: %s", esp_err_to_name(ret));
        return;
    }
    _level = (gpio & 0x01) ? 1.0f : 0.0f;

    ESP_LOGI(TAG, "addr=0x%02X GPIO0=%s", _i2c_addr, (_level > 0.5f ? "HIGH" : "LOW"));
    report_metric("level", _level, _tag_collection);
}

float MCP23008Sensor::getLevel() const {
    return _level;
}


