#pragma once

#include "i2c_sensor.h"
#include "esp_err.h"
#include "communication.h"
#include <string>

/**
 * @brief Panasonic AMG8833 8x8 IR Array (Grid-EYE)
 *
 * Reference: Panasonic AMG8833 datasheet (Grid-EYE) [ADI8000C66]
 * Address conflicts: default I2C address is 0x69, which may also be
 * used by other devices (e.g., SEN55). Use probe() to distinguish.
 */
class AMG8833Sensor : public I2CSensor {
public:
    AMG8833Sensor();
    ~AMG8833Sensor() override;

    // I2CSensor interface
    uint8_t addr() const override;
    std::string name() const override;
    bool init() override;
    bool init(i2c_master_bus_handle_t bus_handle) override;
    void poll() override;
    bool isInitialized() const override;
    uint32_t poll_interval_ms() const override { return 100; }
    bool probe(i2c_master_bus_handle_t bus_handle) override;

    // Data accessors
    float getThermistorCelsius() const;
    void getPixelsCelsius(float out_pixels[64]) const;

private:
    // AMG8833 default I2C address
    static constexpr uint8_t AMG8833_ADDR = 0x69;

    // Register map (typical per datasheet)
    static constexpr uint8_t REG_PCTL        = 0x00; // Power control
    static constexpr uint8_t REG_RST         = 0x01; // Reset
    static constexpr uint8_t REG_FPSC        = 0x02; // Frame rate
    static constexpr uint8_t REG_INTC        = 0x03; // Interrupt control
    static constexpr uint8_t REG_STAT        = 0x04; // Status
    static constexpr uint8_t REG_SCLR        = 0x05; // Status clear
    static constexpr uint8_t REG_AVE         = 0x07; // Moving average
    static constexpr uint8_t REG_TTHL        = 0x0E; // Thermistor low byte
    static constexpr uint8_t REG_TTHH        = 0x0F; // Thermistor high byte
    static constexpr uint8_t REG_PIXEL_BASE  = 0x80; // Pixel 0 low byte

    // Power control values
    static constexpr uint8_t PCTL_NORMAL_MODE   = 0x00;
    static constexpr uint8_t PCTL_SLEEP_MODE    = 0x10;
    static constexpr uint8_t PCTL_STANDBY_60S   = 0x20;
    static constexpr uint8_t PCTL_STANDBY_10S   = 0x21;

    // Reset values
    static constexpr uint8_t RST_FLAG_RESET     = 0x30;
    static constexpr uint8_t RST_INITIAL_RESET  = 0x3F;

    // FPS values
    static constexpr uint8_t FPSC_10FPS         = 0x00;
    static constexpr uint8_t FPSC_1FPS          = 0x01;

    // Read/Write helpers
    esp_err_t writeRegister(uint8_t reg, uint8_t value);
    esp_err_t readRegister(uint8_t reg, uint8_t &value);
    esp_err_t readBlock(uint8_t start_reg, uint8_t *buf, size_t len);

    // Conversion helpers
    static float convertThermistorRaw(uint16_t raw_le);
    static float convertPixelRaw(uint16_t raw_le);

    // State
    bool _initialized = false;
    float _thermistor_c = 0.0f;
    float _pixels_c[64] = {0};
    TagCollection* _tag_collection = nullptr;
};


