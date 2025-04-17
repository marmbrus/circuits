#pragma once

#include "i2c_master_ext.h"
#include <string>

/**
 * @brief Abstract base class for I2C sensors
 * 
 * This class defines the common interface for all I2C sensors in the system.
 * Specific sensor implementations should inherit from this class and implement
 * the required methods.
 */
class I2CSensor {
public:
    /**
     * @brief Construct a new I2C Sensor object
     * 
     * @param bus_handle Handle to the I2C master bus
     */
    I2CSensor(i2c_master_bus_handle_t bus_handle);
    
    /**
     * @brief Destroy the I2C Sensor object
     */
    virtual ~I2CSensor() = default;
    
    /**
     * @brief Get the I2C address of the sensor
     * 
     * @return uint8_t I2C address of the sensor
     */
    virtual uint8_t addr() const = 0;
    
    /**
     * @brief Get the name of the sensor
     * 
     * @return std::string Sensor name
     */
    virtual std::string name() const = 0;
    
    /**
     * @brief Initialize the sensor
     * 
     * @return true If initialization was successful
     * @return false If initialization failed
     */
    virtual bool init() = 0;
    
    /**
     * @brief Initialize the sensor with a bus handle
     * 
     * @param bus_handle Handle to the I2C master bus
     * @return true If initialization was successful
     * @return false If initialization failed
     */
    virtual bool init(i2c_master_bus_handle_t bus_handle) = 0;
    
    /**
     * @brief Poll the sensor for new data
     * 
     * Each sensor should implement this method to read its data
     * and handle any events internally.
     */
    virtual void poll() = 0;
    
    /**
     * @brief Check if the sensor is initialized
     * 
     * @return true If the sensor is initialized
     * @return false If the sensor is not initialized
     */
    virtual bool isInitialized() const = 0;

protected:
    i2c_master_bus_handle_t _bus_handle; ///< Handle to the I2C master bus
    i2c_master_dev_handle_t _dev_handle; ///< Handle to the I2C device
}; 