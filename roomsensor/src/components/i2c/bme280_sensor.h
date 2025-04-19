#pragma once

#include "i2c_sensor.h"
#include <esp_err.h>
#include "communication.h"  // For TagCollection

/**
 * @brief BME280 environmental sensor for temperature, pressure, and humidity
 */
class BME280Sensor : public I2CSensor {
public:
    /**
     * @brief Construct a new BME280 Sensor object
     */
    BME280Sensor();

    /**
     * @brief Destroy the BME280 Sensor object
     */
    ~BME280Sensor();

    /**
     * @brief Get the I2C address of the sensor
     *
     * @return uint8_t I2C address of the sensor
     */
    uint8_t addr() const override;

    /**
     * @brief Get the name of the sensor
     *
     * @return std::string Sensor name
     */
    std::string name() const override;

    /**
     * @brief Initialize the sensor with base init method (not implemented)
     *
     * @return true Never returns true, use the other init() method instead
     * @return false Always returns false
     */
    bool init() override;

    /**
     * @brief Initialize the sensor with bus handle
     *
     * @param bus_handle Handle to the I2C master bus
     * @return true If initialization was successful
     * @return false If initialization failed
     */
    bool init(i2c_master_bus_handle_t bus_handle) override;

    /**
     * @brief Poll the sensor for new data
     */
    void poll() override;

    /**
     * @brief Check if the sensor is initialized
     *
     * @return true If the sensor is initialized
     * @return false If the sensor is not initialized
     */
    bool isInitialized() const override;

    /**
     * @brief Check if the sensor has an interrupt that needs polling
     * 
     * BME280 doesn't support interrupts, so this always returns false
     * 
     * @return false BME280 doesn't support interrupts
     */
    bool hasInterruptTriggered() override { return false; }

    /**
     * @brief Clear the interrupt flag after polling
     * 
     * BME280 doesn't support interrupts, so this does nothing
     */
    void clearInterruptFlag() override {}

    /**
     * @brief Get the temperature in degrees Celsius
     *
     * @return float Temperature in degrees Celsius
     */
    float getTemperature() const;

    /**
     * @brief Get the temperature in degrees Fahrenheit
     *
     * @return float Temperature in degrees Fahrenheit
     */
    float getTemperatureFahrenheit() const;

    /**
     * @brief Get the pressure in hPa
     *
     * @return float Pressure in hPa
     */
    float getPressure() const;

    /**
     * @brief Get the humidity in %RH
     *
     * @return float Humidity in %RH
     */
    float getHumidity() const;

private:
    // Device address (can be 0x76 or 0x77 depending on SDO pin)
    static constexpr uint8_t BME280_I2C_ADDR = 0x76;
    static constexpr uint8_t BME280_CHIP_ID = 0x60; // BME280 has chip ID 0x60

    // Register addresses
    static constexpr uint8_t REG_CHIP_ID = 0xD0;
    static constexpr uint8_t REG_RESET = 0xE0;
    static constexpr uint8_t REG_CTRL_HUM = 0xF2;
    static constexpr uint8_t REG_STATUS = 0xF3;
    static constexpr uint8_t REG_CTRL_MEAS = 0xF4;
    static constexpr uint8_t REG_CONFIG = 0xF5;
    static constexpr uint8_t REG_PRESS_MSB = 0xF7;
    static constexpr uint8_t REG_PRESS_LSB = 0xF8;
    static constexpr uint8_t REG_PRESS_XLSB = 0xF9;
    static constexpr uint8_t REG_TEMP_MSB = 0xFA;
    static constexpr uint8_t REG_TEMP_LSB = 0xFB;
    static constexpr uint8_t REG_TEMP_XLSB = 0xFC;
    static constexpr uint8_t REG_HUM_MSB = 0xFD;
    static constexpr uint8_t REG_HUM_LSB = 0xFE;

    // Calibration registers
    static constexpr uint8_t REG_CALIB_T1_LSB = 0x88;
    static constexpr uint8_t REG_CALIB_H1 = 0xA1;
    static constexpr uint8_t REG_CALIB_H2_LSB = 0xE1;

    // Sensor modes
    static constexpr uint8_t MODE_SLEEP = 0x00;
    static constexpr uint8_t MODE_FORCED = 0x01;
    static constexpr uint8_t MODE_NORMAL = 0x03;

    // Oversampling options
    static constexpr uint8_t OSRS_OFF = 0x00;
    static constexpr uint8_t OSRS_X1 = 0x01;
    static constexpr uint8_t OSRS_X2 = 0x02;
    static constexpr uint8_t OSRS_X4 = 0x03;
    static constexpr uint8_t OSRS_X8 = 0x04;
    static constexpr uint8_t OSRS_X16 = 0x05;

    // Filter coefficients
    static constexpr uint8_t FILTER_OFF = 0x00;
    static constexpr uint8_t FILTER_X2 = 0x01;
    static constexpr uint8_t FILTER_X4 = 0x02;
    static constexpr uint8_t FILTER_X8 = 0x03;
    static constexpr uint8_t FILTER_X16 = 0x04;

    // Standby time
    static constexpr uint8_t STANDBY_0_5_MS = 0x00;
    static constexpr uint8_t STANDBY_62_5_MS = 0x01;
    static constexpr uint8_t STANDBY_125_MS = 0x02;
    static constexpr uint8_t STANDBY_250_MS = 0x03;
    static constexpr uint8_t STANDBY_500_MS = 0x04;
    static constexpr uint8_t STANDBY_1000_MS = 0x05;
    static constexpr uint8_t STANDBY_10_MS = 0x06;
    static constexpr uint8_t STANDBY_20_MS = 0x07;

    // Calibration data structure
    struct CalibrationData {
        uint16_t dig_T1;
        int16_t dig_T2;
        int16_t dig_T3;
        uint16_t dig_P1;
        int16_t dig_P2;
        int16_t dig_P3;
        int16_t dig_P4;
        int16_t dig_P5;
        int16_t dig_P6;
        int16_t dig_P7;
        int16_t dig_P8;
        int16_t dig_P9;
        uint8_t dig_H1;
        int16_t dig_H2;
        uint8_t dig_H3;
        int16_t dig_H4;
        int16_t dig_H5;
        int8_t dig_H6;
    };

    // Private methods
    esp_err_t readRegister(uint8_t reg, uint8_t *data, size_t len = 1);
    esp_err_t writeRegister(uint8_t reg, uint8_t value);
    esp_err_t readCalibrationData();
    esp_err_t setSensorMode(uint8_t mode);
    esp_err_t readRawData();

    // Temperature compensation
    int32_t compensateTemperature(int32_t adc_T);

    // Pressure compensation
    uint32_t compensatePressure(int32_t adc_P);

    // Humidity compensation
    uint32_t compensateHumidity(int32_t adc_H);

    // Member variables
    bool _initialized;
    CalibrationData _calibData;
    int32_t _t_fine; // Used for both temperature and pressure compensation

    // Measurement results
    float _temperature;
    float _pressure;
    float _humidity;

    // Tag collection
    TagCollection* _tag_collection;
};