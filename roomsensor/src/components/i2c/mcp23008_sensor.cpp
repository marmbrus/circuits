#include "mcp23008_sensor.h"
#include "i2c_master_ext.h"
#include "esp_log.h"
#include "ConfigurationManager.h"
#include "IOConfig.h"
#include "mcp23088_keypad.h"

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

    // Determine io index and config reference
    _io_index = addrToIndex(_i2c_addr);
    if (_io_index >= 1 && _io_index <= 8) {
        config::ConfigurationManager &mgr = config::GetConfigurationManager();
        switch (_io_index) {
            case 1: _config_ptr = &mgr.io1(); break; case 2: _config_ptr = &mgr.io2(); break;
            case 3: _config_ptr = &mgr.io3(); break; case 4: _config_ptr = &mgr.io4(); break;
            case 5: _config_ptr = &mgr.io5(); break; case 6: _config_ptr = &mgr.io6(); break;
            case 7: _config_ptr = &mgr.io7(); break; case 8: _config_ptr = &mgr.io8(); break;
        }
    }

    // Ensure effective matches base before first apply, then force-write configuration to hardware
    if (_config_ptr) {
        _config_ptr->reset_effective_switches_to_base();
    }
    configureFromConfig(true);

    _tag_collection = create_tag_collection();
    if (_tag_collection == nullptr) {
        ESP_LOGE(TAG, "Failed to create tag collection");
        return false;
    }
    add_tag_to_collection(_tag_collection, "type", "mcp23008");
    char addr_buf[8]; snprintf(addr_buf, sizeof(addr_buf), "0x%02X", _i2c_addr);
    add_tag_to_collection(_tag_collection, "addr", addr_buf);

    _initialized = true;
    poll();
    return true;
}

int MCP23008Sensor::addrToIndex(uint8_t addr) const {
    if (addr < 0x20 || addr > 0x27) return -1;
    return (addr - 0x20) + 1; // 0x20->1, ... 0x27->8
}

void MCP23008Sensor::configureFromConfig(bool force_write) {
    // Start from reset defaults
    uint8_t iodir = 0xFF; // all inputs
    uint8_t gppu  = 0x00; // pull-ups disabled
    uint8_t olat  = _olat_cached; // preserve last outputs if not overridden

    // Build direction/pullups/outputs based on IOConfig
    if (_config_ptr) {
        for (int i = 0; i < 8; ++i) {
            auto mode = _config_ptr->pin_mode(i + 1);
            if (mode == config::IOConfig::PinMode::SWITCH ||
                mode == config::IOConfig::PinMode::SWITCH_HIGH ||
                mode == config::IOConfig::PinMode::SWITCH_LOW) {
                // Output
                iodir &= (uint8_t)~(1u << i); // bit=0 => output
                // pull-up irrelevant for output
                bool desired = _config_ptr->switch_state(i + 1);
                bool is_set = _config_ptr->is_switch_state_set(i + 1);
                // Determine electrical level for ON based on mode
                bool on_drives_low = (mode == config::IOConfig::PinMode::SWITCH || mode == config::IOConfig::PinMode::SWITCH_LOW);
                if (!is_set) {
                    // Default OFF
                    desired = false;
                }
                bool drive_low = desired ? on_drives_low : !on_drives_low;
                if (drive_low) olat &= (uint8_t)~(1u << i); else olat |= (uint8_t)(1u << i);
            } else if (mode == config::IOConfig::PinMode::SENSOR) {
                // Input with pull-up
                iodir |= (uint8_t)(1u << i); // bit=1 => input
                gppu  |= (uint8_t)(1u << i); // enable pull-up
            }
        }
    } else {
        // Default to all inputs with pull-ups for safety
        gppu = 0xFF;
    }

    // Only write registers if changes detected, unless force_write requests full sync
    if (force_write || iodir != _iodir_cached) { writeRegister(REG_IODIR, iodir); _iodir_cached = iodir; }
    if (force_write || gppu  != _gppu_cached)  { writeRegister(REG_GPPU,  gppu);  _gppu_cached  = gppu; }
    if (force_write || olat  != _olat_cached)  { writeRegister(REG_OLAT,  olat);  _olat_cached  = olat; }
}

void MCP23008Sensor::poll() {
    if (!_initialized) return;

    // Re-apply configuration in case of runtime changes
    configureFromConfig();

    uint8_t gpio = 0;
    esp_err_t ret = readRegister(REG_GPIO, gpio);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read GPIO: %s", esp_err_to_name(ret));
        return;
    }
    // Compare with last state and report changes for SENSOR pins only.
    // Also update IOConfig contact states for SENSOR pins.
    uint8_t changed = gpio ^ _gpio_cached_last;
    bool any_contact_change = false;
    for (int i = 0; i < 8; ++i) {
        config::IOConfig::PinMode mode = config::IOConfig::PinMode::INVALID;
        if (_config_ptr) mode = _config_ptr->pin_mode(i + 1);

        bool bit_now_high = ((gpio >> i) & 0x01) != 0;
        bool is_low_now = !bit_now_high; // active low => closed when low

        if (_config_ptr && mode == config::IOConfig::PinMode::SENSOR) {
            _config_ptr->set_contact_state(i + 1, is_low_now);
        }

        if (((changed >> i) & 0x01) == 0) continue; // no electrical change on this pin

        if (mode == config::IOConfig::PinMode::SENSOR) {
            // Update tag with pin index (1..8)
            char index_buf[4]; snprintf(index_buf, sizeof(index_buf), "%d", i + 1);
            add_tag_to_collection(_tag_collection, "index", index_buf);
            const char* pname = _config_ptr ? _config_ptr->pin_name(i + 1) : "";
            if (pname && pname[0] != '\0') {
                add_tag_to_collection(_tag_collection, "name", pname);
            }
            ESP_LOGI(TAG, "io%d pin%d contact %s", _io_index, i + 1, is_low_now ? "closed" : "open");
            report_metric("contact", is_low_now ? 1.0f : 0.0f, _tag_collection);
            any_contact_change = true;
        }
    }
    _gpio_cached_last = gpio;
    // Maintain _level for backward compatibility with existing callers
    _level = (gpio & 0x01) ? 1.0f : 0.0f;

    bool logic_changed_outputs = false;
    // Apply optional per-module logic and re-apply outputs if it changed anything
    if (_config_ptr && _config_ptr->is_logic_set()) {
        switch (_config_ptr->logic()) {
            case config::IOConfig::Logic::LOCK_KEYPAD: {
                char modname[16]; snprintf(modname, sizeof(modname), "io%d", _io_index);
                bool changed = i2c_logic::apply_lock_keypad_logic(*_config_ptr, modname);
                if (changed) {
                    ESP_LOGI(TAG, "Logic LOCK_KEYPAD changed switch states on %s; reapplying outputs", modname);
                    configureFromConfig();
                    logic_changed_outputs = true;
                }
                break;
            }
            case config::IOConfig::Logic::NONE:
            default:
                break;
        }
    }

    // Verify outputs if logic changed anything
    if (logic_changed_outputs) {
        uint8_t olat_read = 0;
        if (readRegister(REG_OLAT, olat_read) == ESP_OK) {
            ESP_LOGD(TAG, "io%d OLAT after logic: 0x%02X", _io_index, olat_read);
        } else {
            ESP_LOGW(TAG, "io%d failed to read back OLAT after logic", _io_index);
        }
    }

    // Publish once at startup with initial states, and thereafter on any contact change or effective switch change.
    bool any_effective_change = false;
    if (_config_ptr) {
        static uint8_t last_set_mask = 0, last_on_mask = 0;
        uint8_t set_mask = 0, on_mask = 0;
        _config_ptr->get_effective_switch_snapshot(set_mask, on_mask);
        if (set_mask != last_set_mask || on_mask != last_on_mask) {
            any_effective_change = true;
            last_set_mask = set_mask;
            last_on_mask = on_mask;
        }
    }

    if (!_initial_state_published) {
        config::GetConfigurationManager().publish_full_configuration();
        _initial_state_published = true;
    } else if (any_contact_change || any_effective_change) {
        config::GetConfigurationManager().publish_full_configuration();
    }
}

float MCP23008Sensor::getLevel() const {
    return _level;
}


