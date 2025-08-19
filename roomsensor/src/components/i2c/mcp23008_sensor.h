#pragma once

#include "i2c_sensor.h"
#include "esp_err.h"
#include "communication.h"
#include <string>

/**
 * MCP23008 8-bit I2C GPIO expander (Microchip)
 * Simple driver that reports the state of GPIO0 as an input.
 */
class MCP23008Sensor : public I2CSensor {
public:
    explicit MCP23008Sensor(uint8_t i2c_address);
    ~MCP23008Sensor() override;

    uint8_t addr() const override;
    std::string name() const override;

    bool init() override;
    bool init(i2c_master_bus_handle_t bus_handle) override;
    void poll() override;
    bool isInitialized() const override;

    bool hasInterruptTriggered() override { return false; }
    void clearInterruptFlag() override {}

    float getLevel() const;

private:
    // Register map
    static constexpr uint8_t REG_IODIR = 0x00;
    static constexpr uint8_t REG_IPOL  = 0x01;
    static constexpr uint8_t REG_GPINTEN = 0x02;
    static constexpr uint8_t REG_DEFVAL = 0x03;
    static constexpr uint8_t REG_INTCON = 0x04;
    static constexpr uint8_t REG_IOCON = 0x05;
    static constexpr uint8_t REG_GPPU  = 0x06;
    static constexpr uint8_t REG_INTF  = 0x07;
    static constexpr uint8_t REG_INTCAP= 0x08;
    static constexpr uint8_t REG_GPIO  = 0x09;
    static constexpr uint8_t REG_OLAT  = 0x0A;

    // I2C helpers for 8-bit registers
    esp_err_t writeRegister(uint8_t reg, uint8_t value);
    esp_err_t readRegister(uint8_t reg, uint8_t &value);

    void configureGpio0AsInput();

    uint8_t _i2c_addr;
    bool _initialized;
    float _level; // 0.0 or 1.0 based on GPIO0
    TagCollection* _tag_collection;
};


