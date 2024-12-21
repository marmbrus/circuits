#pragma once

#include <esp_err.h>
#include "driver/i2c_master.h"
#include "i2c_master_ext.h"

#ifdef __cplusplus
extern "C" {
#endif

// Device address
#define LIS2DH12_I2C_ADDR          0x18    // SA0 pin to VDD
#define LIS2DH12_ID                0x33    // Who am I value

// Registers
#define LIS2DH12_WHO_AM_I          0x0F
#define LIS2DH12_CTRL_REG1         0x20
#define LIS2DH12_CTRL_REG2         0x21
#define LIS2DH12_CTRL_REG3         0x22
#define LIS2DH12_CTRL_REG4         0x23
#define LIS2DH12_CTRL_REG5         0x24
#define LIS2DH12_CTRL_REG6         0x25
#define LIS2DH12_STATUS_REG        0x27
#define LIS2DH12_OUT_X_L           0x28
#define LIS2DH12_OUT_X_H           0x29
#define LIS2DH12_OUT_Y_L           0x2A
#define LIS2DH12_OUT_Y_H           0x2B
#define LIS2DH12_OUT_Z_L           0x2C
#define LIS2DH12_OUT_Z_H           0x2D
#define LIS2DH12_INT1_CFG          0x30
#define LIS2DH12_INT1_SRC          0x31
#define LIS2DH12_INT1_THS          0x32
#define LIS2DH12_INT1_DURATION     0x33

// Data rates
typedef enum {
    LIS2DH12_POWER_DOWN            = 0x00,
    LIS2DH12_ODR_1Hz              = 0x01,
    LIS2DH12_ODR_10Hz             = 0x02,
    LIS2DH12_ODR_25Hz             = 0x03,
    LIS2DH12_ODR_50Hz             = 0x04,
    LIS2DH12_ODR_100Hz            = 0x05,
    LIS2DH12_ODR_200Hz            = 0x06,
    LIS2DH12_ODR_400Hz            = 0x07
} lis2dh12_odr_t;

// Scale selection
typedef enum {
    LIS2DH12_2G                   = 0x00,
    LIS2DH12_4G                   = 0x01,
    LIS2DH12_8G                   = 0x02,
    LIS2DH12_16G                  = 0x03
} lis2dh12_scale_t;

// Operating modes
typedef enum {
    LIS2DH12_HR_12BIT             = 0x00,
    LIS2DH12_NM_10BIT             = 0x01,
    LIS2DH12_LP_8BIT              = 0x02
} lis2dh12_mode_t;

// Acceleration data structure
typedef struct {
    float x;
    float y;
    float z;
} lis2dh12_accel_t;

/**
 * @brief Initialize the LIS2DH12 sensor
 *
 * @param i2c_handle I2C master bus handle
 * @return esp_err_t ESP_OK on success
 */
esp_err_t lis2dh12_init(i2c_master_bus_handle_t i2c_handle);

/**
 * @brief Set the data rate of the sensor
 *
 * @param rate Data rate selection
 * @return esp_err_t ESP_OK on success
 */
esp_err_t lis2dh12_set_data_rate(lis2dh12_odr_t rate);

/**
 * @brief Set the full scale range
 *
 * @param scale Scale selection
 * @return esp_err_t ESP_OK on success
 */
esp_err_t lis2dh12_set_scale(lis2dh12_scale_t scale);

/**
 * @brief Set the operating mode
 *
 * @param mode Operating mode selection
 * @return esp_err_t ESP_OK on success
 */
esp_err_t lis2dh12_set_mode(lis2dh12_mode_t mode);

/**
 * @brief Read acceleration data
 *
 * @param accel Pointer to acceleration data structure
 * @return esp_err_t ESP_OK on success
 */
esp_err_t lis2dh12_get_accel(lis2dh12_accel_t *accel);

/**
 * @brief Check if new acceleration data is available
 *
 * @param available Pointer to store availability status
 * @return esp_err_t ESP_OK on success
 */
esp_err_t lis2dh12_data_ready(bool *available);

/**
 * @brief Configure the LIS2DH12 for movement detection interrupt
 *
 * This function configures the LIS2DH12's interrupt system to detect movement/acceleration
 * above a certain threshold. The interrupt is configured as follows:
 *
 * - Threshold: Set to detect accelerations > 0.125g (5 * 16-bit LSB * 0.001g/LSB)
 * - Duration: Minimum duration (1 sample) to avoid false triggers
 * - Configuration: OR combination of high events on X, Y, Z axes
 * - Interrupt routing: INT1 signal routed to INT1 pin
 *
 * The threshold calculation depends on the current scale and mode:
 * In High Resolution mode (12-bit) with ±2g scale:
 * - 1 LSB = 1mg
 * - Threshold register value = desired_g * (16/scale)
 * - Example: For 0.125g = 0.125 * (16/2) = 1
 *
 * Duration is set in ODR cycles (1/ODR):
 * - At 50 Hz ODR, 1 cycle = 20ms
 * - Duration = cycles * (1/ODR)
 *
 * @note Assumes the sensor is already configured in High Resolution mode
 * @note INT1 pin must be properly connected to GPIO1 for interrupt detection
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t lis2dh12_configure_movement_interrupt();

/**
 * @brief Read the interrupt source register to check what caused the interrupt
 *
 * @param src Pointer to store the interrupt source value
 * @return esp_err_t ESP_OK on success
 */
esp_err_t lis2dh12_get_int1_source(uint8_t *src);

/**
 * @brief Check if the interrupt configuration is still valid and reconfigure if needed
 *
 * This function checks if the interrupt configuration has been maintained and
 * reconfigures it if necessary. This helps prevent situations where the interrupt
 * configuration might be lost due to power management or other system events.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t lis2dh12_check_interrupt_config(void);

#ifdef __cplusplus
}
#endif