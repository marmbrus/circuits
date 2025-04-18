#include "lis2dh.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "lis2dh";

static i2c_master_bus_handle_t i2c_bus = NULL;
static i2c_master_dev_handle_t dev_handle = NULL;
static lis2dh12_scale_t current_scale = LIS2DH12_2G;
static lis2dh12_mode_t current_mode = LIS2DH12_HR_12BIT;

static float last_x = 0, last_y = 0, last_z = 0;
static const float ORIENTATION_THRESHOLD = 0.8f;
static const float MOVEMENT_THRESHOLD = 0.1f;

// Define the current orientation variable
device_orientation_t current_orientation = ORIENTATION_UNKNOWN;

// Function to determine orientation from accelerometer data
device_orientation_t determine_orientation(float x, float y, float z) {
    if (fabsf(x) > ORIENTATION_THRESHOLD) {
        return (x > 0) ? ORIENTATION_TOP : ORIENTATION_BOTTOM;
    }
    if (fabsf(y) > ORIENTATION_THRESHOLD) {
        return (y > 0) ? ORIENTATION_RIGHT : ORIENTATION_LEFT;
    }
    if (fabsf(z) > ORIENTATION_THRESHOLD) {
        return (z > 0) ? ORIENTATION_UP : ORIENTATION_DOWN;
    }
    return ORIENTATION_UNKNOWN;
}

bool is_significant_movement(float x, float y, float z) {
    bool significant = (
        fabsf(x - last_x) > MOVEMENT_THRESHOLD ||
        fabsf(y - last_y) > MOVEMENT_THRESHOLD ||
        fabsf(z - last_z) > MOVEMENT_THRESHOLD
    );

    last_x = x;
    last_y = y;
    last_z = z;

    return significant;
}

static esp_err_t write_register(uint8_t reg, uint8_t value)
{
    uint8_t write_buf[2] = {reg, value};
    esp_err_t ret = i2c_master_transmit(dev_handle, write_buf, 2, I2C_XFR_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write register 0x%02x: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t read_register(uint8_t reg, uint8_t *value)
{
    esp_err_t ret = i2c_master_transmit_receive(dev_handle, &reg, 1, value, 1, I2C_XFR_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read register 0x%02x: %s", reg, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t lis2dh12_init(i2c_master_bus_handle_t i2c_handle)
{
    esp_err_t ret;
    uint8_t whoami;

    i2c_bus = i2c_handle;

    // Configure device
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LIS2DH12_I2C_ADDR,
        .scl_speed_hz = 400000,
    };

    // Create device handle
    ret = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device to I2C bus");
        return ret;
    }

    // Check device ID
    ret = read_register(LIS2DH12_WHO_AM_I, &whoami);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WHO_AM_I register");
        return ret;
    }

    if (whoami != LIS2DH12_ID) {
        ESP_LOGE(TAG, "Invalid WHO_AM_I value: 0x%02x", whoami);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Configure default settings
    // Enable all axes, normal mode, 50Hz
    ret = write_register(LIS2DH12_CTRL_REG1, 0x57);
    if (ret != ESP_OK) return ret;

    // High resolution mode (12-bit) and ±2g range
    // BDU=1 (Block Data Update), HR=1 (High Resolution), FS=00 (±2g)
    ret = write_register(LIS2DH12_CTRL_REG4, 0x88);
    if (ret != ESP_OK) return ret;

    // Configure CTRL_REG2 for high-pass filter on interrupts only
    // 0x09 = 00001001b
    // Bits 7-6: 00 = High-pass filter normal mode
    // Bits 5-4: 00 = Highest cutoff frequency
    // Bits 3-0: 1001 = Enable filter on INT1
    ret = write_register(LIS2DH12_CTRL_REG2, 0x09);
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "LIS2DH12 initialized successfully");
    return ESP_OK;
}

esp_err_t lis2dh12_set_data_rate(lis2dh12_odr_t rate)
{
    uint8_t reg;
    esp_err_t ret = read_register(LIS2DH12_CTRL_REG1, &reg);
    if (ret != ESP_OK) return ret;

    reg &= ~(0xF0);  // Clear ODR bits
    reg |= (rate << 4);

    return write_register(LIS2DH12_CTRL_REG1, reg);
}

esp_err_t lis2dh12_set_scale(lis2dh12_scale_t scale)
{
    uint8_t reg;
    esp_err_t ret = read_register(LIS2DH12_CTRL_REG4, &reg);
    if (ret != ESP_OK) return ret;

    reg &= ~(0x30);  // Clear FS bits while preserving other bits
    reg |= (scale << 4);
    reg |= 0x88;     // Ensure BDU and HR bits are set

    ret = write_register(LIS2DH12_CTRL_REG4, reg);
    if (ret == ESP_OK) {
        current_scale = scale;
    }
    return ret;
}

esp_err_t lis2dh12_set_mode(lis2dh12_mode_t mode)
{
    uint8_t reg1, reg4;
    esp_err_t ret = read_register(LIS2DH12_CTRL_REG1, &reg1);
    if (ret != ESP_OK) return ret;

    ret = read_register(LIS2DH12_CTRL_REG4, &reg4);
    if (ret != ESP_OK) return ret;

    switch (mode) {
        case LIS2DH12_HR_12BIT:
            reg1 &= ~(0x08);  // Clear LPen bit
            reg4 |= (0x08);   // Set HR bit
            break;
        case LIS2DH12_NM_10BIT:
            reg1 &= ~(0x08);  // Clear LPen bit
            reg4 &= ~(0x08);  // Clear HR bit
            break;
        case LIS2DH12_LP_8BIT:
            reg1 |= (0x08);   // Set LPen bit
            reg4 &= ~(0x08);  // Clear HR bit
            break;
    }

    ret = write_register(LIS2DH12_CTRL_REG1, reg1);
    if (ret != ESP_OK) return ret;

    ret = write_register(LIS2DH12_CTRL_REG4, reg4);
    if (ret == ESP_OK) {
        current_mode = mode;
    }
    return ret;
}

esp_err_t lis2dh12_get_accel(lis2dh12_accel_t *accel)
{
    uint8_t data[6];
    int16_t raw_x, raw_y, raw_z;
    float sensitivity;
    esp_err_t ret;

    // Read all acceleration registers in one transaction
    uint8_t reg = LIS2DH12_OUT_X_L | 0x80;  // Set MSB for multi-byte read
    ret = i2c_master_transmit_receive(dev_handle, &reg, 1, data, 6, I2C_XFR_TIMEOUT_MS);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read acceleration data: %s", esp_err_to_name(ret));
        return ret;
    }

    // Combine high and low bytes and sign extend the 12-bit values
    raw_x = ((int16_t)(data[1] << 8 | data[0])) >> 4;
    raw_y = ((int16_t)(data[3] << 8 | data[2])) >> 4;
    raw_z = ((int16_t)(data[5] << 8 | data[4])) >> 4;

    ESP_LOGD(TAG, "Raw Accel Data: X=%d Y=%d Z=%d", raw_x, raw_y, raw_z);

    // Calculate sensitivity based on current scale and mode
    switch (current_mode) {
        case LIS2DH12_HR_12BIT:
            switch (current_scale) {
                case LIS2DH12_2G:  sensitivity = 0.001f;  break;  // 1 mg/LSB
                case LIS2DH12_4G:  sensitivity = 0.002f;  break;  // 2 mg/LSB
                case LIS2DH12_8G:  sensitivity = 0.004f;  break;  // 4 mg/LSB
                case LIS2DH12_16G: sensitivity = 0.012f;  break;  // 12 mg/LSB
                default: return ESP_ERR_INVALID_STATE;
            }
            break;
        default:
            return ESP_ERR_INVALID_STATE;
    }

    // Convert to g's
    accel->x = raw_x * sensitivity;
    accel->y = raw_y * sensitivity;
    accel->z = raw_z * sensitivity;

    ESP_LOGD(TAG, "Converted Accel Data: X=%.2f Y=%.2f Z=%.2f g", accel->x, accel->y, accel->z);

    return ESP_OK;
}

esp_err_t lis2dh12_data_ready(bool *available)
{
    uint8_t status;
    esp_err_t ret = read_register(LIS2DH12_STATUS_REG, &status);
    if (ret != ESP_OK) return ret;

    *available = (status & 0x08) != 0;  // Check ZYXDA bit
    return ESP_OK;
}

// Add a new function to periodically check and reconfigure the interrupt if needed
esp_err_t lis2dh12_check_interrupt_config()
{
    uint8_t reg;
    esp_err_t ret;

    // Check if interrupt is still properly configured
    ret = read_register(LIS2DH12_CTRL_REG3, &reg);
    if (ret != ESP_OK) return ret;

    if (reg != 0x40) {
        ESP_LOGW(TAG, "Interrupt configuration lost, reconfiguring...");
        
        // Directly reconfigure the interrupt since lis2dh12_configure_movement_interrupt was moved
        // Temporarily disable all interrupts
        ret = write_register(LIS2DH12_CTRL_REG3, 0x00);
        if (ret != ESP_OK) return ret;

        // Reset INT1_CFG
        ret = write_register(LIS2DH12_INT1_CFG, 0x00);
        if (ret != ESP_OK) return ret;

        // Clear any pending interrupts
        ret = read_register(LIS2DH12_INT1_SRC, &reg);
        if (ret != ESP_OK) return ret;

        // Configure CTRL_REG2 for high-pass filter
        ret = write_register(LIS2DH12_CTRL_REG2, 0x01);
        if (ret != ESP_OK) return ret;

        // Configure CTRL_REG1 (50Hz, all axes enabled)
        ret = write_register(LIS2DH12_CTRL_REG1, 0x57);
        if (ret != ESP_OK) return ret;

        // Configure CTRL_REG4 for high resolution
        ret = write_register(LIS2DH12_CTRL_REG4, 0x88);
        if (ret != ESP_OK) return ret;

        // Set interrupt threshold and duration
        ret = write_register(LIS2DH12_INT1_THS, 5);
        if (ret != ESP_OK) return ret;
        ret = write_register(LIS2DH12_INT1_DURATION, 0);
        if (ret != ESP_OK) return ret;

        // Configure INT1_CFG
        ret = write_register(LIS2DH12_INT1_CFG, 0x2A);
        if (ret != ESP_OK) return ret;

        // Enable interrupt on INT1 pin
        ret = write_register(LIS2DH12_CTRL_REG3, 0x40);
        if (ret != ESP_OK) return ret;

        vTaskDelay(pdMS_TO_TICKS(10));  // Small delay
    }

    return ESP_OK;
}

esp_err_t lis2dh12_get_int1_source(uint8_t *src)
{
    // Reading INT1_SRC register clears the interrupt
    return read_register(LIS2DH12_INT1_SRC, src);
}

esp_err_t lis2dh12_configure_normal_mode(void)
{
    esp_err_t ret;

    // Disable interrupts temporarily
    ret = write_register(LIS2DH12_CTRL_REG3, 0x00);
    if (ret != ESP_OK) return ret;

    // Configure CTRL_REG2 to disable high-pass filter
    ret = write_register(LIS2DH12_CTRL_REG2, 0x00);
    if (ret != ESP_OK) return ret;

    // Set data rate and enable all axes in CTRL_REG1
    // 0x57 = 01010111b (50Hz, all axes enabled)
    ret = write_register(LIS2DH12_CTRL_REG1, 0x57);
    if (ret != ESP_OK) return ret;

    // Configure CTRL_REG4 for high resolution mode and ±2g range
    // 0x88 = 10001000b (BDU enabled, HR mode, ±2g range)
    ret = write_register(LIS2DH12_CTRL_REG4, 0x88);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

esp_err_t lis2dh12_configure_sleep_mode(void)
{
    // This function previously called configure_movement_interrupt
    // Now we'll implement the functionality directly
    esp_err_t ret;
    uint8_t reg;

    // Temporarily disable all interrupts
    ret = write_register(LIS2DH12_CTRL_REG3, 0x00);
    if (ret != ESP_OK) return ret;

    // Reset INT1_CFG
    ret = write_register(LIS2DH12_INT1_CFG, 0x00);
    if (ret != ESP_OK) return ret;

    // Clear any pending interrupts
    ret = read_register(LIS2DH12_INT1_SRC, &reg);
    if (ret != ESP_OK) return ret;

    // Configure CTRL_REG2 for high-pass filter
    ret = write_register(LIS2DH12_CTRL_REG2, 0x01);
    if (ret != ESP_OK) return ret;

    // Configure CTRL_REG1 (50Hz, all axes enabled)
    ret = write_register(LIS2DH12_CTRL_REG1, 0x57);
    if (ret != ESP_OK) return ret;

    // Configure CTRL_REG4 for high resolution
    ret = write_register(LIS2DH12_CTRL_REG4, 0x88);
    if (ret != ESP_OK) return ret;

    // Set interrupt threshold and duration
    ret = write_register(LIS2DH12_INT1_THS, 5);
    if (ret != ESP_OK) return ret;
    ret = write_register(LIS2DH12_INT1_DURATION, 0);
    if (ret != ESP_OK) return ret;

    // Configure INT1_CFG
    ret = write_register(LIS2DH12_INT1_CFG, 0x2A);
    if (ret != ESP_OK) return ret;

    // Enable interrupt on INT1 pin
    ret = write_register(LIS2DH12_CTRL_REG3, 0x40);
    if (ret != ESP_OK) return ret;

    return ESP_OK;
}

// Make the read_register function public
esp_err_t lis2dh12_read_register(uint8_t reg, uint8_t *value)
{
    return read_register(reg, value);
}