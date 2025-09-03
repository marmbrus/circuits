#pragma once

#include "i2c_sensor.h"
#include "esp_err.h"
#include "communication.h"
#include <string>

/**
 * @brief SEN55 sensor class
 *
 * This class implements the I2CSensor interface for the Sensirion SEN55
 * environmental sensor, which measures particulate matter, VOC, NOx,
 * temperature, and humidity.
 */
class SEN55Sensor : public I2CSensor {
public:
    /**
     * @brief Construct a new SEN55Sensor object
     */
    SEN55Sensor();

    /**
     * @brief Destroy the SEN55Sensor object
     */
    ~SEN55Sensor() override;

    /**
     * @brief Get the I2C address of the sensor
     *
     * @return uint8_t I2C address
     */
    uint8_t addr() const override;

    /**
     * @brief Get the name of the sensor
     *
     * @return std::string Name
     */
    std::string name() const override;

    /**
     * @brief Check if the sensor is initialized
     *
     * @return true If initialized
     * @return false If not initialized
     */
    bool isInitialized() const override;

    /**
     * @brief Check if the sensor has an interrupt that needs polling
     *
     * SEN55 doesn't support interrupts, so this always returns false
     *
     * @return false SEN55 doesn't support interrupts
     */
    bool hasInterruptTriggered() override { return false; }

    /**
     * @brief Clear the interrupt flag after polling
     *
     * SEN55 doesn't support interrupts, so this does nothing
     */
    void clearInterruptFlag() override {}

    /**
     * @brief Initialize the sensor (requires bus handle through other init method)
     *
     * @return true If successful
     * @return false If failed
     */
    bool init() override;

    /**
     * @brief Initialize the sensor with a bus handle
     *
     * @param bus_handle Handle to the I2C master bus
     * @return true If successful
     * @return false If failed
     */
    bool init(i2c_master_bus_handle_t bus_handle) override;

    /**
     * @brief Poll the sensor for new data
     */
    void poll() override;

    /**
     * @brief Get the mass concentration for PM1.0
     *
     * @return float PM1.0 in μg/m³
     */
    float getPM1() const;

    /**
     * @brief Get the mass concentration for PM2.5
     *
     * @return float PM2.5 in μg/m³
     */
    float getPM2_5() const;

    /**
     * @brief Get the mass concentration for PM4.0
     *
     * @return float PM4.0 in μg/m³
     */
    float getPM4() const;

    /**
     * @brief Get the mass concentration for PM10
     *
     * @return float PM10 in μg/m³
     */
    float getPM10() const;

    /**
     * @brief Get the volatile organic compounds index
     *
     * @return float VOC index (dimensionless)
     */
    float getVOC() const;

    /**
     * @brief Get the nitrogen oxide index
     *
     * @return float NOx index (dimensionless)
     */
    float getNOx() const;

    /**
     * @brief Get the temperature
     *
     * @return float Temperature in °C
     */
    float getTemperature() const;

    /**
     * @brief Get the humidity
     *
     * @return float Relative humidity in %
     */
    float getHumidity() const;

    /**
     * @brief Get the temperature in Fahrenheit
     *
     * @return float Temperature in °F
     */
    float getTemperatureFahrenheit() const;

    /**
     * @brief Check if temperature reading is valid
     *
     * @return true If temperature reading is valid
     * @return false If temperature reading is not available
     */
    bool isTemperatureValid() const;

    /**
     * @brief Check if humidity reading is valid
     *
     * @return true If humidity reading is valid
     * @return false If humidity reading is not available
     */
    bool isHumidityValid() const;

private:
    // I2C communication
    esp_err_t sendCommand(uint16_t command);
    esp_err_t sendCommandWithArgs(uint16_t command, const uint8_t* args, size_t args_len);
    esp_err_t readMeasurement();

    // Helper methods
    uint8_t calculateCRC(const uint8_t* data, size_t length) const;

    // Sensor data
    float _pm1;       ///< PM1.0 in μg/m³
    float _pm2_5;     ///< PM2.5 in μg/m³
    float _pm4;       ///< PM4.0 in μg/m³
    float _pm10;      ///< PM10 in μg/m³
    float _voc;       ///< VOC index
    float _nox;       ///< NOx index
    float _temperature; ///< Temperature in °C
    float _humidity;    ///< Humidity in %

    bool _initialized;  ///< Initialization state
    TagCollection* _tag_collection; ///< Tag collection for metrics

    // Constants for SEN55
    static constexpr uint8_t SEN55_I2C_ADDR = 0x69;

    // SEN55 commands
    static constexpr uint16_t CMD_START_MEASUREMENT = 0x0021;
    static constexpr uint16_t CMD_START_MEASUREMENT_WITH_ARGS = 0x0021;
    static constexpr uint16_t CMD_STOP_MEASUREMENT = 0x0104;
    static constexpr uint16_t CMD_READ_MEASUREMENT = 0x03C4;
    static constexpr uint16_t CMD_READ_DEVICE_INFO = 0xD014;
    static constexpr uint16_t CMD_RESET = 0xD304;

    bool _temperature_valid;   ///< Whether temperature reading is valid
    bool _humidity_valid;      ///< Whether humidity reading is valid

    int _startup_readings_count; // Counter for startup readings to suppress initial warnings

    // Warming-up tagging
    int _warmup_remaining = 10; // number of polls to tag as warming_up
};