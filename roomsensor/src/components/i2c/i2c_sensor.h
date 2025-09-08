#pragma once

#include "i2c_master_ext.h"
#include "esp_timer.h"
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

    /**
     * @brief Probe the device at this sensor's address to verify identity.
     *
     * Default implementation returns true (best-effort) so legacy sensors
     * continue to work. Sensors that can positively identify themselves
     * should override and return false when they know the device is not
     * their expected chip.
     */
    virtual bool probe(i2c_master_bus_handle_t bus_handle) { (void)bus_handle; return true; }

    /**
     * @brief Desired periodic polling interval in milliseconds.
     *
     * Default is 10 seconds for most sensors. Sensors that need
     * higher-frequency sampling (e.g., IO expanders used for contact
     * detection) should override to return a smaller interval.
     */
    virtual uint32_t poll_interval_ms() const { return 10000; }

    /**
     * @brief Check if the sensor has an interrupt that needs polling
     * 
     * This method should be implemented by sensors that support interrupts
     * to indicate when they need to be polled due to an interrupt.
     * After polling, the interrupt flag should be cleared.
     * 
     * @return true If the sensor has triggered an interrupt
     * @return false If no interrupt is triggered
     */
    virtual bool hasInterruptTriggered() { return false; }

    /**
     * @brief Clear the interrupt flag after polling
     * 
     * This method should be called after polling a sensor that had
     * an interrupt triggered to reset its interrupt state.
     */
    virtual void clearInterruptFlag() {}

    /**
     * @brief Optional logical index for sensors that can appear multiple times
     *        (e.g., ADS1115 a2d1..a2d4, MCP23008 io1..io8). Returns -1 if not applicable.
     */
    virtual int index() const { return -1; }

    /**
     * @brief Optional configuration module name associated with this sensor instance
     *        (e.g., "a2d1" or "io1"). Returns empty string if not applicable.
     */
    virtual std::string config_module_name() const { return std::string(); }


protected:
    i2c_master_bus_handle_t _bus_handle; ///< Handle to the I2C master bus
    i2c_master_dev_handle_t _dev_handle; ///< Handle to the I2C device
    // Shared warm-up configuration for all I2C sensors
    // Avoid reporting metrics for this duration after sensor initialization
    static constexpr uint32_t I2C_SENSOR_WARMUP_MS = 3 * 60 * 1000; // 3 minutes
    unsigned long long _init_time_ms = 0; ///< Time in ms when the sensor finished init

    // Helper exposed to derived classes: returns true when within the warm-up window
    bool is_warming_up() const {
        if (_init_time_ms == 0) return true;
        unsigned long long now_ms = static_cast<unsigned long long>(esp_timer_get_time() / 1000);
        return (now_ms - _init_time_ms) < I2C_SENSOR_WARMUP_MS;
    }
}; 