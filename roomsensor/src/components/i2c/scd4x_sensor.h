#pragma once

#include "i2c_sensor.h"
#include "esp_err.h"
#include "communication.h"
#include <string>

/**
 * @brief SCD4x sensor class
 *
 * This class implements the I2CSensor interface for the Sensirion SCD4x
 * CO₂ sensor family (SCD40, SCD41). It measures CO₂ concentration,
 * temperature, and humidity.
 */
class SCD4xSensor : public I2CSensor {
public:
    /**
     * @brief Construct a new SCD4xSensor object
     */
    SCD4xSensor();

    /**
     * @brief Destroy the SCD4xSensor object
     */
    ~SCD4xSensor() override;

    /**
     * @brief Get the I2C address of the sensor (fixed at 0x62 for SCD4x)
     */
    uint8_t addr() const override;

    /**
     * @brief Get the name of the sensor
     */
    std::string name() const override;

    /**
     * @brief Check if the sensor is initialized
     */
    bool isInitialized() const override;

    /**
     * @brief Check if the sensor has an interrupt that needs polling
     * 
     * SCD4x doesn't support interrupts, so this always returns false
     */
    bool hasInterruptTriggered() override { return false; }

    /**
     * @brief Clear the interrupt flag after polling
     * 
     * SCD4x doesn't support interrupts, so this does nothing
     */
    void clearInterruptFlag() override {}

    /**
     * @brief Initialize the sensor (requires bus handle)
     */
    bool init() override;

    /**
     * @brief Initialize the sensor with a bus handle
     */
    bool init(i2c_master_bus_handle_t bus_handle) override;

    /**
     * @brief Poll the sensor for new data
     */
    void poll() override;

    /**
     * @brief Get the CO₂ concentration in ppm
     */
    float getCO2() const;

    /**
     * @brief Get the temperature in °C
     */
    float getTemperature() const;

    /**
     * @brief Get the relative humidity in %
     */
    float getHumidity() const;

    /**
     * @brief Get the temperature in Fahrenheit
     */
    float getTemperatureFahrenheit() const;

private:
    // I2C communication commands for SCD4x
    esp_err_t sendCommand(uint16_t command);
    esp_err_t readMeasurement();

    uint8_t calculateCRC(const uint8_t* data, size_t length) const;

    float _co2;          ///< CO₂ concentration in ppm
    float _temperature;  ///< Temperature in °C
    float _humidity;     ///< Relative humidity in %
    bool _initialized;   ///< Initialization state
    TagCollection* _tag_collection; ///< Tag collection for metrics

    // SCD4x I2C address
    static constexpr uint8_t SCD4X_I2C_ADDR = 0x62;

    // SCD4x commands
    static constexpr uint16_t CMD_START_PERIODIC_MEASUREMENT   = 0x21B1;
    static constexpr uint16_t CMD_READ_MEASUREMENT             = 0xEC05;
    static constexpr uint16_t CMD_STOP_PERIODIC_MEASUREMENT    = 0x3F86;
    static constexpr uint16_t CMD_RESET                        = 0x94A2;
};