#include "lis2dh_sensor.h"
#include "esp_log.h"
#include <string.h>
#include <math.h>
#include "i2c.h"
#include "driver/gpio.h"
#include "esp_timer.h"  // Add include for esp_timer_get_time
#include <inttypes.h>   // Add include for PRIu32 format specifier
#include "communication.h" // Add include for metrics reporting

static const char *TAG = "LIS2DHSensor";

// ISR handler - NOT static to match the friend declaration in the header
void IRAM_ATTR lis2dh_isr_handler(void* arg)
{
    // Mark the sensor as having its interrupt triggered
    LIS2DHSensor* sensor = static_cast<LIS2DHSensor*>(arg);
    if (sensor) {
        sensor->_interruptTriggered = true;
        // Note: We'll process the actual interrupt source register in poll()
        // since we can't do I2C operations safely from an ISR
    }
    
    // Call the signalSensorInterrupt function when interrupt triggers
    signalSensorInterrupt();
}

LIS2DHSensor::LIS2DHSensor() :
    I2CSensor(nullptr),
    _bus_handle(nullptr),
    _lastAccel{0, 0, 0},
    _movementDetected(false),
    _initialized(false),
    _tag_collection(nullptr),
    _tag_collection_x(nullptr),
    _tag_collection_y(nullptr),
    _tag_collection_z(nullptr),
    _interruptTriggered(false),
    _lastPollTime(0),
    _xAxisTriggerCount(0),
    _yAxisTriggerCount(0),
    _zAxisTriggerCount(0),
    _maxMagnitude(0.0f),
    _hasInterruptData(false) {
    ESP_LOGD(TAG, "LIS2DHSensor constructed");
}

LIS2DHSensor::~LIS2DHSensor() {
    if (_tag_collection != nullptr) {
        free_tag_collection(_tag_collection);
        _tag_collection = nullptr;
    }
    if (_tag_collection_x != nullptr) {
        free_tag_collection(_tag_collection_x);
        _tag_collection_x = nullptr;
    }
    if (_tag_collection_y != nullptr) {
        free_tag_collection(_tag_collection_y);
        _tag_collection_y = nullptr;
    }
    if (_tag_collection_z != nullptr) {
        free_tag_collection(_tag_collection_z);
        _tag_collection_z = nullptr;
    }
}

uint8_t LIS2DHSensor::addr() const {
    return LIS2DH12_I2C_ADDR;
}

std::string LIS2DHSensor::name() const {
    return "LIS2DH12 Motion Sensor";
}

bool LIS2DHSensor::isInitialized() const {
    if (!_initialized) {
        ESP_LOGD(TAG, "Sensor not initialized. Call init() first.");
    }
    return _initialized;
}

// Internal methods that don't check initialization status
esp_err_t LIS2DHSensor::_writeRegister(uint8_t reg, uint8_t value) {
    uint8_t write_buf[2] = {reg, value};
    esp_err_t ret = i2c_master_transmit(_dev_handle, write_buf, 2, 100); // 100ms timeout
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write register 0x%02x: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t LIS2DHSensor::_readRegister(uint8_t reg, uint8_t *value) {
    esp_err_t ret = i2c_master_transmit_receive(_dev_handle, &reg, 1, value, 1, 100); // 100ms timeout
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read register 0x%02x: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

// Public methods that check initialization status
esp_err_t LIS2DHSensor::writeRegister(uint8_t reg, uint8_t value) {
    if (!isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    return _writeRegister(reg, value);
}

esp_err_t LIS2DHSensor::readRegister(uint8_t reg, uint8_t *value) {
    if (!isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    return _readRegister(reg, value);
}

bool LIS2DHSensor::init() {
    ESP_LOGE(TAG, "Invalid init() call without bus handle. Use init(i2c_master_bus_handle_t) instead.");
    return false;
}

bool LIS2DHSensor::init(i2c_master_bus_handle_t bus_handle) {
    if (_initialized) {
        ESP_LOGW(TAG, "Sensor already initialized");
        return true;
    }

    if (bus_handle == nullptr) {
        ESP_LOGE(TAG, "Invalid bus handle (null)");
        return false;
    }

    _bus_handle = bus_handle;

    ESP_LOGI(TAG, "Initializing LIS2DH12 accelerometer");

    esp_err_t ret;
    uint8_t whoami;

    // Configure device
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LIS2DH12_I2C_ADDR,
        .scl_speed_hz = 400000,
        .scl_wait_us = 0,     // Add missing initializer
        .flags = 0            // Add missing initializer
    };

    // Create device handle
    ret = i2c_master_bus_add_device(_bus_handle, &dev_cfg, &_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device to I2C bus: %s", esp_err_to_name(ret));
        return false;
    }

    // Check device ID - use internal methods that don't check initialization status
    ret = _readRegister(WHO_AM_I, &whoami);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WHO_AM_I register: %s", esp_err_to_name(ret));
        return false;
    }

    if (whoami != LIS2DH12_ID) {
        ESP_LOGE(TAG, "Invalid WHO_AM_I value: 0x%02x", whoami);
        return false;
    }

    // Configure default settings - use internal methods
    // Enable all axes, normal mode, 50Hz (0x57 = 01010111b)
    ret = _writeRegister(CTRL_REG1, 0x57);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure CTRL_REG1: %s", esp_err_to_name(ret));
        return false;
    }

    // High resolution mode (12-bit) and ±2g range
    // BDU=1 (Block Data Update), HR=1 (High Resolution), FS=00 (±2g)
    // 0x88 = 10001000b
    ret = _writeRegister(CTRL_REG4, 0x88);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure CTRL_REG4: %s", esp_err_to_name(ret));
        return false;
    }

    // Create tag collection for metrics reporting
    _tag_collection = create_tag_collection();
    if (_tag_collection == nullptr) {
        ESP_LOGE(TAG, "Failed to create tag collection");
        return false;
    }

    // Add sensor-specific tags
    esp_err_t err_type = add_tag_to_collection(_tag_collection, "type", "lis2dh");
    esp_err_t err_name = add_tag_to_collection(_tag_collection, "name", "accel");

    if (err_type != ESP_OK || err_name != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add tags to collection");
        free_tag_collection(_tag_collection);
        _tag_collection = nullptr;
        return false;
    }
    
    // Create axis-specific tag collections
    _tag_collection_x = create_tag_collection();
    _tag_collection_y = create_tag_collection();
    _tag_collection_z = create_tag_collection();
    
    if (_tag_collection_x == nullptr || _tag_collection_y == nullptr || _tag_collection_z == nullptr) {
        ESP_LOGE(TAG, "Failed to create axis tag collections");
        if (_tag_collection_x) free_tag_collection(_tag_collection_x);
        if (_tag_collection_y) free_tag_collection(_tag_collection_y);
        if (_tag_collection_z) free_tag_collection(_tag_collection_z);
        _tag_collection_x = _tag_collection_y = _tag_collection_z = nullptr;
        free_tag_collection(_tag_collection);
        _tag_collection = nullptr;
        return false;
    }
    
    // Copy base tags to each axis collection
    for (int i = 0; i < _tag_collection->count; i++) {
        add_tag_to_collection(_tag_collection_x, _tag_collection->tags[i].key, _tag_collection->tags[i].value);
        add_tag_to_collection(_tag_collection_y, _tag_collection->tags[i].key, _tag_collection->tags[i].value);
        add_tag_to_collection(_tag_collection_z, _tag_collection->tags[i].key, _tag_collection->tags[i].value);
    }
    
    // Add axis-specific tags
    add_tag_to_collection(_tag_collection_x, "axis", "x");
    add_tag_to_collection(_tag_collection_y, "axis", "y");
    add_tag_to_collection(_tag_collection_z, "axis", "z");

    // Mark as initialized BEFORE configuring sleep mode which uses public methods
    _initialized = true;

    // Configure movement interrupt instead of sleep mode
    if (configureMovementInterrupt() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure movement interrupt");
        return false;
    }

    // Configure GPIO pin for LIS2DH INT1 interrupt (IO13)
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << 13);  // IO13
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;  // Pull down when inactive
    io_conf.intr_type = GPIO_INTR_POSEDGE;        // Interrupt on rising edge

    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO for interrupt: %s", esp_err_to_name(ret));
        return false;
    }

    // Install GPIO ISR service if not already installed
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {  // ESP_ERR_INVALID_STATE means already installed
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
        return false;
    }

    // Add handler for GPIO interrupt - pass 'this' pointer to access member variables
    ret = gpio_isr_handler_add((gpio_num_t)13, lis2dh_isr_handler, this);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add GPIO ISR handler: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "LIS2DH12 accelerometer initialized successfully with interrupt on IO13");
    return true;
}

esp_err_t LIS2DHSensor::getAccelData(AccelData &accel) {
    if (!isInitialized()) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[6];
    int16_t raw_x, raw_y, raw_z;
    float sensitivity = 0.001f; // 1 mg/LSB for ±2g in high resolution mode

    // Read all acceleration registers in one transaction
    uint8_t reg = OUT_X_L | 0x80;  // Set MSB for multi-byte read
    esp_err_t ret = i2c_master_transmit_receive(_dev_handle, &reg, 1, data, 6, 100); // 100ms timeout

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read acceleration data: %s", esp_err_to_name(ret));
        return ret;
    }

    // Combine high and low bytes and sign extend the 12-bit values
    raw_x = ((int16_t)(data[1] << 8 | data[0])) >> 4;
    raw_y = ((int16_t)(data[3] << 8 | data[2])) >> 4;
    raw_z = ((int16_t)(data[5] << 8 | data[4])) >> 4;

    // Convert to g's
    accel.x = raw_x * sensitivity;
    accel.y = raw_y * sensitivity;
    accel.z = raw_z * sensitivity;

    ESP_LOGD(TAG, "Accel Data: X=%.3f Y=%.3f Z=%.3f g", accel.x, accel.y, accel.z);

    return ESP_OK;
}

void LIS2DHSensor::poll() {
    if (!isInitialized()) {
        ESP_LOGE(TAG, "Cannot poll: sensor not initialized");
        return;
    }

    // Get current time in milliseconds
    uint32_t current_time = esp_timer_get_time() / 1000;
    
    // Read acceleration data to calculate magnitude
    AccelData accel;
    esp_err_t ret = getAccelData(accel);
    
    if (ret == ESP_OK) {
        // Calculate magnitude of acceleration
        float magnitude = calculateMagnitude(accel.x, accel.y, accel.z);
        
        // Update maximum magnitude if necessary
        if (magnitude > _maxMagnitude) {
            _maxMagnitude = magnitude;
        }
    }
    
    // Check interrupt source register
    uint8_t int_source = 0;
    ret = readRegister(INT1_SRC, &int_source);
    
    if (ret == ESP_OK) {
        // Log the raw register for debugging
        ESP_LOGD(TAG, "INT1_SRC register: 0x%02x (IA:%d, ZH:%d, ZL:%d, YH:%d, YL:%d, XH:%d, XL:%d)",
                 int_source,
                 (int_source & INT_ACTIVE) ? 1 : 0,
                 (int_source & INT_Z_HIGH) ? 1 : 0,
                 (int_source & INT_Z_LOW) ? 1 : 0,
                 (int_source & INT_Y_HIGH) ? 1 : 0,
                 (int_source & INT_Y_LOW) ? 1 : 0,
                 (int_source & INT_X_HIGH) ? 1 : 0,
                 (int_source & INT_X_LOW) ? 1 : 0);
                 
        if (int_source & INT_ACTIVE) {
            // Process which axes triggered
            processInterruptSource(int_source);
        }
    }
    
    // Check if we're within the minimum polling interval
    if (current_time - _lastPollTime < MIN_POLL_INTERVAL_MS) {
        ESP_LOGD(TAG, "Within polling interval, accumulating data");
        return;
    }
    
    // Outside the polling interval, time to report accumulated data
    if (_hasInterruptData) {
        ESP_LOGI(TAG, "Movement detected: X:%lu Y:%lu Z:%lu triggers, max magnitude: %.3f g", 
                 _xAxisTriggerCount, _yAxisTriggerCount, _zAxisTriggerCount, _maxMagnitude);
        
        // Set the movement detected flag for API consistency
        _movementDetected = true;
        
        // Report metrics for each axis count with simplified tag approach
        static const char* METRIC_AXIS_TRIGGERS = "accel_triggers";
        static const char* METRIC_AXIS_VALUE = "accel_value";
        static const char* METRIC_MAX_MAG = "accel_max_magnitude";
        
        // First report the max magnitude which doesn't need axis tag
        report_metric(METRIC_MAX_MAG, _maxMagnitude, _tag_collection);
        
        // Use the pre-created tag collections with axis tags
        if (_tag_collection_x && _tag_collection_y && _tag_collection_z) {
            // Report metrics for X axis
            report_metric(METRIC_AXIS_TRIGGERS, (float)_xAxisTriggerCount, _tag_collection_x);
            report_metric(METRIC_AXIS_VALUE, accel.x, _tag_collection_x);
            
            // Report metrics for Y axis
            report_metric(METRIC_AXIS_TRIGGERS, (float)_yAxisTriggerCount, _tag_collection_y);
            report_metric(METRIC_AXIS_VALUE, accel.y, _tag_collection_y);
            
            // Report metrics for Z axis
            report_metric(METRIC_AXIS_TRIGGERS, (float)_zAxisTriggerCount, _tag_collection_z);
            report_metric(METRIC_AXIS_VALUE, accel.z, _tag_collection_z);
        } else {
            ESP_LOGE(TAG, "Axis tag collections not available");
        }
        
        // Report overall movement detection metric
        static const char* METRIC_MOVEMENT = "accel_detected";
        report_metric(METRIC_MOVEMENT, 1.0f, _tag_collection);
    } else {
        ESP_LOGD(TAG, "No movement detected since last poll");
        
        // Report zero movement for metrics
        static const char* METRIC_MOVEMENT = "accel_detected";
        report_metric(METRIC_MOVEMENT, 0.0f, _tag_collection);
    }
    
    // Reset counters and flags
    _xAxisTriggerCount = 0;
    _yAxisTriggerCount = 0;
    _zAxisTriggerCount = 0;
    _maxMagnitude = 0.0f;
    _hasInterruptData = false;
    
    // Update the last poll time
    _lastPollTime = current_time;
    
    // Clear the interrupt flag
    _interruptTriggered = false;
}

bool LIS2DHSensor::hasMovement() {
    if (!isInitialized()) {
        ESP_LOGE(TAG, "Cannot check movement: sensor not initialized");
        return false;
    }

    bool movement = _movementDetected;
    _movementDetected = false; // Clear the flag after reading
    return movement;
}

bool LIS2DHSensor::configureSleepMode() {
    if (!isInitialized()) {
        ESP_LOGE(TAG, "Cannot configure sleep mode: sensor not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Configuring LIS2DH12 for movement detection (sleep mode)");

    esp_err_t ret;

    // Temporarily disable all interrupts
    ret = writeRegister(CTRL_REG3, 0x00);
    if (ret != ESP_OK) return false;

    // Reset INT1_CFG first
    ret = writeRegister(INT1_CFG, 0x00);
    if (ret != ESP_OK) return false;

    // Clear any pending interrupts
    uint8_t reg;
    ret = readRegister(INT1_SRC, &reg);
    if (ret != ESP_OK) return false;

    // Configure CTRL_REG2 for high-pass filter
    // 0x01 = Enable HP filter for INT1
    ret = writeRegister(CTRL_REG2, 0x01);
    if (ret != ESP_OK) return false;

    // Set data rate and enable all axes in CTRL_REG1
    // 0x57 = 01010111b (50Hz, all axes enabled)
    ret = writeRegister(CTRL_REG1, 0x57);
    if (ret != ESP_OK) return false;

    // Configure CTRL_REG4 for high resolution mode and ±2g range
    // 0x88 = 10001000b (BDU enabled, HR mode, ±2g range)
    ret = writeRegister(CTRL_REG4, 0x88);
    if (ret != ESP_OK) return false;

    // Set interrupt threshold (adjust if needed)
    ret = writeRegister(INT1_THS, 5);  // ~80mg threshold (5 * 16mg = 80mg)
    if (ret != ESP_OK) return false;

    // Set interrupt duration
    ret = writeRegister(INT1_DURATION, 0);  // No minimum duration
    if (ret != ESP_OK) return false;

    // Configure INT1_CFG for OR combination of high events on all axes
    // 0x2A = 00101010b (OR combination of high events on X, Y, Z)
    ret = writeRegister(INT1_CFG, 0x2A);
    if (ret != ESP_OK) return false;

    // Enable interrupt on INT1 pin in CTRL_REG3
    // 0x40 = 01000000b (INT1 interrupt enabled)
    ret = writeRegister(CTRL_REG3, 0x40);
    if (ret != ESP_OK) return false;

    ESP_LOGI(TAG, "LIS2DH12 configured for movement detection");
    return true;
}

bool LIS2DHSensor::configureNormalMode() {
    if (!isInitialized()) {
        ESP_LOGE(TAG, "Cannot configure normal mode: sensor not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Configuring LIS2DH12 for normal mode");

    esp_err_t ret;

    // Disable interrupts temporarily
    ret = writeRegister(CTRL_REG3, 0x00);
    if (ret != ESP_OK) return false;

    // Configure CTRL_REG2 to disable high-pass filter
    ret = writeRegister(CTRL_REG2, 0x00);
    if (ret != ESP_OK) return false;

    // Set data rate and enable all axes in CTRL_REG1
    // 0x57 = 01010111b (50Hz, all axes enabled)
    ret = writeRegister(CTRL_REG1, 0x57);
    if (ret != ESP_OK) return false;

    // Configure CTRL_REG4 for high resolution mode and ±2g range
    // 0x88 = 10001000b (BDU enabled, HR mode, ±2g range)
    ret = writeRegister(CTRL_REG4, 0x88);
    if (ret != ESP_OK) return false;

    ESP_LOGI(TAG, "LIS2DH12 configured for normal mode");
    return true;
}

esp_err_t LIS2DHSensor::configureMovementInterrupt() {
    if (!isInitialized()) {
        ESP_LOGE(TAG, "Cannot configure interrupt: sensor not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;
    uint8_t reg;

    // Temporarily disable all interrupts
    ret = writeRegister(CTRL_REG3, 0x00);
    if (ret != ESP_OK) return ret;

    // Reset INT1_CFG first
    ret = writeRegister(INT1_CFG, 0x00);
    if (ret != ESP_OK) return ret;

    // Clear any pending interrupts
    ret = readRegister(INT1_SRC, &reg);
    if (ret != ESP_OK) return ret;

    // Configure CTRL_REG2 for high-pass filter
    // 0x01 = Enable HP filter for INT1
    ret = writeRegister(CTRL_REG2, 0x01);
    if (ret != ESP_OK) return ret;

    // Set data rate and enable all axes in CTRL_REG1
    // 0x57 = 01010111b (50Hz, all axes enabled)
    ret = writeRegister(CTRL_REG1, 0x57);
    if (ret != ESP_OK) return ret;

    // Configure CTRL_REG4 for high resolution mode and ±2g range
    // 0x88 = 10001000b (BDU enabled, HR mode, ±2g range)
    ret = writeRegister(CTRL_REG4, 0x88);
    if (ret != ESP_OK) return ret;

    // Set interrupt threshold (adjust if needed)
    ret = writeRegister(INT1_THS, 5);  // ~80mg threshold (5 * 16mg = 80mg)
    if (ret != ESP_OK) return ret;

    // Set interrupt duration
    ret = writeRegister(INT1_DURATION, 0);  // No minimum duration
    if (ret != ESP_OK) return ret;

    // Configure INT1_CFG for OR combination of high events on all axes
    // 0x2A = 00101010b (OR combination of high events on X, Y, Z)
    ret = writeRegister(INT1_CFG, 0x2A);
    if (ret != ESP_OK) return ret;

    // Enable interrupt on INT1 pin in CTRL_REG3
    // 0x40 = 01000000b (INT1 interrupt enabled)
    ret = writeRegister(CTRL_REG3, 0x40);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "Movement interrupt configured for LIS2DH12");
    return ESP_OK;
}

bool LIS2DHSensor::hasInterruptTriggered() {
    return _interruptTriggered;
}

void LIS2DHSensor::clearInterruptFlag() {
    _interruptTriggered = false;
}

// Calculate magnitude of acceleration vector
float LIS2DHSensor::calculateMagnitude(float x, float y, float z) {
    return sqrtf(x*x + y*y + z*z);
}

// Process interrupt source register and update axis counters
void LIS2DHSensor::processInterruptSource(uint8_t int_source) {
    // We already logged the full register in poll(), no need to repeat here
    
    // Process each axis independently - only count HIGH events since that's what we configured
    bool anyTriggered = false;
    
    // Check X-axis HIGH event only
    if (int_source & INT_X_HIGH) {
        _xAxisTriggerCount++;
        ESP_LOGD(TAG, "X-axis HIGH event triggered");
        anyTriggered = true;
    }
    
    // Check Y-axis HIGH event only
    if (int_source & INT_Y_HIGH) {
        _yAxisTriggerCount++;
        ESP_LOGD(TAG, "Y-axis HIGH event triggered");
        anyTriggered = true;
    }
    
    // Check Z-axis HIGH event only
    if (int_source & INT_Z_HIGH) {
        _zAxisTriggerCount++;
        ESP_LOGD(TAG, "Z-axis HIGH event triggered");
        anyTriggered = true;
    }
    
    if (anyTriggered) {
        _hasInterruptData = true;
    }
}