#ifndef __TAS5825M_H__
#define __TAS5825M_H__

#include <esp_err.h>
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"

// TAS5825M I2S pins
#define TAS5825M_SDIN_GPIO     11
#define TAS5825M_SCLK_GPIO     12
#define TAS5825M_LRCLK_GPIO    13
#define TAS5825M_PDN_GPIO      14      // Add PDN (Power Down) GPIO

// I2S configuration
#define TAS5825M_SAMPLE_RATE   48000
#define TAS5825M_BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_16BIT
#define TAS5825M_CHANNEL_FMT   I2S_SLOT_MODE_STEREO

// TAS5825M I2C address
#define TAS5825M_I2C_ADDR      0x4C

// TAS5825M Register addresses
#define TAS5825M_REG_PAGE      0x00
#define TAS5825M_REG_BOOK      0x7F
#define TAS5825M_REG_RESET     0x01
#define TAS5825M_REG_POWER     0x02
#define TAS5825M_REG_MUTE      0x03
#define TAS5825M_REG_VOL       0x04
#define TAS5825M_REG_DIGI_CLK  0x05    // Digital Clock Control
#define TAS5825M_REG_SIG_CH    0x28    // Input MUX control
#define TAS5825M_REG_RESET_CTRL    0x01    // Reset Control register
#define TAS5825M_REG_DEVICE_CTRL1  0x02    // Device Control 1 register
#define TAS5825M_REG_DEVICE_CTRL2  0x03    // Device Control 2 register
#define TAS5825M_REG_SAP_CTRL1     0x33    // SAP Control 1 register
#define TAS5825M_REG_DSP_PGM_MODE  0x40    // DSP Program Mode register
#define TAS5825M_REG_DIG_VOL       0x4C    // Digital Volume register
#define TAS5825M_REG_GPIO_CTRL     0x60    // GPIO Control register
#define TAS5825M_REG_GPIO1_SEL     0x62    // GPIO1 Select register
#define TAS5825M_REG_GPIO2_SEL     0x63    // GPIO2 Select register
#define TAS5825M_REG_CLKDET_STATUS 0x39    // Clock Detection Status register
#define TAS5825M_REG_GLOBAL_FAULT1 0x71    // Global Fault 1 register
#define TAS5825M_REG_GLOBAL_FAULT2 0x72    // Global Fault 2 register
#define TAS5825M_REG_WARNING       0x73    // Warning register
#define TAS5825M_REG_FAULT_CLEAR   0x78    // Fault Clear register

// Device states for DEVICE_CTRL2 register
#define TAS5825M_STATE_DEEP_SLEEP    0x00
#define TAS5825M_STATE_SLEEP         0x01
#define TAS5825M_STATE_HIZ           0x02
#define TAS5825M_STATE_PLAY          0x03

// GPIO function selections
#define TAS5825M_GPIO_FUNC_FAULTZ    0x0B
#define TAS5825M_GPIO_FUNC_WARNZ     0x08

/**
 * @brief Initialize the TAS5825M amplifier
 *
 * @param bus_handle I2C port handle
 * @return esp_err_t ESP_OK if successful
 */
esp_err_t tas5825m_init(i2c_master_bus_handle_t bus_handle);

/**
 * @brief Play a test tone through the TAS5825M
 *
 * @return esp_err_t ESP_OK if successful
 */
esp_err_t tas5825m_play_test_tone(void);

/**
 * @brief Play a WAV file through the TAS5825M
 *
 * @return esp_err_t ESP_OK if successful
 */
esp_err_t tas5825m_play_wav(void);

#endif // __TAS5825M_H__