#pragma once

#include "i2c_sensor.h"
#include <esp_err.h>
#include "communication.h" // Add include for TagCollection

/**
 * @brief LIS2DH accelerometer sensor implementation
 *
 * This class provides an interface to the LIS2DH accelerometer sensor,
 * which can detect movement and measure acceleration along three axes.
 * Movement events are handled internally in the poll() method.
 */
class LIS2DHSensor : public I2CSensor {
public:
    /**
     * @brief Construct a new LIS2DH Sensor object
     */
    LIS2DHSensor();

    /**
     * @brief Destructor for LIS2DH Sensor object
     */
    virtual ~LIS2DHSensor();

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
     * @brief Initialize the sensor with the default init method (not implemented)
     *
     * @return true Never returns true, use the other init() method instead
     * @return false Always returns false
     */
    bool init() override;

    /**
     * @brief Initialize the sensor with a bus handle
     *
     * @param bus_handle Handle to the I2C master bus
     * @return true If initialization was successful
     * @return false If initialization failed
     */
    bool init(i2c_master_bus_handle_t bus_handle) override;

    /**
     * @brief Poll the sensor for new data and check for movement events
     *
     * This method reads the sensor data and detects any movement events.
     * Movement events are processed internally within this method.
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
     * @return true If the sensor has an interrupt triggered
     * @return false If no interrupt is triggered
     */
    bool hasInterruptTriggered() override;

    /**
     * @brief Clear the interrupt flag after polling
     */
    void clearInterruptFlag() override;

    /**
     * @brief Check if movement was detected
     *
     * @return true If movement was detected
     * @return false If no movement was detected
     */
    bool hasMovement();

    /**
     * @brief Configure the sensor for power saving (sleep mode)
     *
     * @return true If configuration was successful
     * @return false If configuration failed
     */
    bool configureSleepMode();

    /**
     * @brief Configure the sensor for movement interrupt detection
     *
     * @return esp_err_t ESP_OK if successful, error code otherwise
     */
    esp_err_t configureMovementInterrupt();

    /**
     * @brief Configure the sensor for normal operation
     *
     * @return true If configuration was successful
     * @return false If configuration failed
     */
    bool configureNormalMode();

    /**
     * @brief Write to a register (checks initialization status)
     *
     * @param reg Register address
     * @param value Value to write
     * @return esp_err_t Result of the operation
     */
    esp_err_t writeRegister(uint8_t reg, uint8_t value);

    /**
     * @brief Read from a register (checks initialization status)
     *
     * @param reg Register address
     * @param value Pointer to store the read value
     * @return esp_err_t Result of the operation
     */
    esp_err_t readRegister(uint8_t reg, uint8_t *value);

private:
    // Friend declaration to allow ISR to access private members
    friend void IRAM_ATTR lis2dh_isr_handler(void* arg);
    
    // Device address
    static constexpr uint8_t LIS2DH12_I2C_ADDR = 0x18; // SA0 pin to VDD
    static constexpr uint8_t LIS2DH12_ID = 0x33; // Who am I value

    // Registers
    static constexpr uint8_t WHO_AM_I = 0x0F;
    static constexpr uint8_t CTRL_REG1 = 0x20;
    static constexpr uint8_t CTRL_REG2 = 0x21;
    static constexpr uint8_t CTRL_REG3 = 0x22;
    static constexpr uint8_t CTRL_REG4 = 0x23;
    static constexpr uint8_t CTRL_REG5 = 0x24;
    static constexpr uint8_t STATUS_REG = 0x27;
    static constexpr uint8_t OUT_X_L = 0x28;
    static constexpr uint8_t INT1_CFG = 0x30;
    static constexpr uint8_t INT1_SRC = 0x31;
    static constexpr uint8_t INT1_THS = 0x32;
    static constexpr uint8_t INT1_DURATION = 0x33;

    // Motion detection threshold
    static constexpr float MOVEMENT_THRESHOLD = 0.1f;

    // Internal methods that don't check initialization status
    esp_err_t _writeRegister(uint8_t reg, uint8_t value);
    esp_err_t _readRegister(uint8_t reg, uint8_t *value);

    // Acceleration data structure
    struct AccelData {
        float x;
        float y;
        float z;
    };

    // Read acceleration data from the sensor
    esp_err_t getAccelData(AccelData &accel);

    // Check if movement is significant by comparing with threshold
    bool isSignificantMovement(float x, float y, float z);

    // Member variables
    i2c_master_bus_handle_t _bus_handle; // I2C bus handle
    AccelData _lastAccel; // Last read acceleration data
    bool _movementDetected; // Flag indicating if movement was detected
    bool _initialized; // Flag indicating if the sensor has been initialized
    TagCollection* _tag_collection; // Tag collection for metrics reporting
    
    // Variables for interrupt handling and rate limiting
    bool _interruptTriggered; // Flag indicating if an interrupt was triggered
    uint32_t _lastPollTime;   // Timestamp of the last successful poll (in milliseconds)
    static constexpr uint32_t MIN_POLL_INTERVAL_MS = 1000; // Minimum 1 second between polls
};