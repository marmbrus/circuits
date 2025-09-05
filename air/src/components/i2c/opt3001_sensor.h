#pragma once

#include "i2c_sensor.h"
#include "esp_err.h"
#include "communication.h"
#include <string>

/**
 * OPT3001 Ambient Light Sensor (Texas Instruments)
 */
class OPT3001Sensor : public I2CSensor {
public:
    OPT3001Sensor();
    ~OPT3001Sensor() override;

    uint8_t addr() const override;
    std::string name() const override;

    bool init() override;
    bool init(i2c_master_bus_handle_t bus_handle) override;
    void poll() override;
    bool isInitialized() const override;

    bool hasInterruptTriggered() override { return false; }
    void clearInterruptFlag() override {}

    float getLux() const;

private:
    // Register map
    static constexpr uint8_t REG_RESULT          = 0x00;
    static constexpr uint8_t REG_CONFIG          = 0x01;
    static constexpr uint8_t REG_LOW_LIMIT       = 0x02;
    static constexpr uint8_t REG_HIGH_LIMIT      = 0x03;
    static constexpr uint8_t REG_MANUFACTURER_ID = 0x7E;
    static constexpr uint8_t REG_DEVICE_ID       = 0x7F;

    // Known ID values (per datasheet)
    static constexpr uint16_t MANUFACTURER_ID_TI = 0x5449; // 'TI'
    static constexpr uint16_t DEVICE_ID_OPT3001  = 0x3001;

    // I2C helpers (big-endian 16-bit)
    esp_err_t writeRegister(uint8_t reg, uint16_t value_be);
    esp_err_t readRegister(uint8_t reg, uint16_t &value_be);

    // Configure device for continuous conversions with automatic range
    esp_err_t configureContinuousAutoRange();

    float _lux;
    bool _initialized;
    TagCollection* _tag_collection;

    static constexpr uint8_t OPT3001_I2C_ADDR = 0x44; // ADDR pin grounded
};


