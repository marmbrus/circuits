#pragma once

#include "i2c_sensor.h"
#include <string>

// LMP91000 Electrochemical AFE potentiostat
// Default I2C address commonly used: 0x48 (can vary per board strapping)

class LMP91000Sensor : public I2CSensor {
public:
    explicit LMP91000Sensor(uint8_t i2c_address = 0x48)
        : I2CSensor(nullptr), _i2c_addr(i2c_address), _initialized(false) {}

    ~LMP91000Sensor() override = default;

    uint8_t addr() const override { return _i2c_addr; }
    std::string name() const override {
        char buf[32];
        snprintf(buf, sizeof(buf), "LMP91000@0x%02X", _i2c_addr);
        return std::string(buf);
    }

    bool init() override { return false; }
    bool init(i2c_master_bus_handle_t bus_handle) override;
    void poll() override {}
    bool isInitialized() const override { return _initialized; }

private:
    // LMP91000 register addresses
    static constexpr uint8_t REG_STATUS = 0x00;   // optional
    static constexpr uint8_t REG_LOCK   = 0x01;   // write 0x00 to unlock (if used)
    static constexpr uint8_t REG_TIACN  = 0x10;   // TIA control
    static constexpr uint8_t REG_REFCN  = 0x11;   // Reference control
    static constexpr uint8_t REG_MODECN = 0x12;   // Mode control

    bool write_reg(uint8_t reg, uint8_t val);
    bool read_reg(uint8_t reg, uint8_t &val);

    uint8_t _i2c_addr;
    bool _initialized;
};


