#pragma once

#include "i2c_sensor.h"
#include <esp_err.h>
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include <string>

// TAS5825M I2S pins
#define TAS5825M_SDIN_GPIO      11
#define TAS5825M_SCLK_GPIO      12
#define TAS5825M_LRCLK_GPIO     13
#define TAS5825M_PDN_GPIO       14

// I2S configuration
#define TAS5825M_SAMPLE_RATE            48000
#define TAS5825M_BITS_PER_SAMPLE        I2S_DATA_BIT_WIDTH_16BIT
#define TAS5825M_CHANNEL_FMT            I2S_SLOT_MODE_STEREO

// TAS5825M I2C address
#define TAS5825M_I2C_ADDR               0x4E

// TAS5825M Register addresses
#define TAS5825M_REG_PAGE               0x00
#define TAS5825M_REG_BOOK               0x7F
#define TAS5825M_REG_RESET              0x01
#define TAS5825M_REG_POWER              0x02
#define TAS5825M_REG_MUTE               0x03
#define TAS5825M_REG_VOL                0x04
#define TAS5825M_REG_DIGI_CLK           0x05
#define TAS5825M_REG_SIG_CH             0x28
#define TAS5825M_REG_RESET_CTRL         0x01
#define TAS5825M_REG_DEVICE_CTRL1       0x02
#define TAS5825M_REG_DEVICE_CTRL2       0x03
#define TAS5825M_REG_SAP_CTRL1          0x33
#define TAS5825M_REG_DSP_PGM_MODE       0x40
#define TAS5825M_REG_DIG_VOL            0x4C
#define TAS5825M_REG_GPIO_CTRL          0x60
#define TAS5825M_REG_GPIO1_SEL          0x62
#define TAS5825M_REG_GPIO2_SEL          0x63
#define TAS5825M_REG_CLKDET_STATUS      0x39
#define TAS5825M_REG_FS_MON              0x37
#define TAS5825M_REG_BCK_MON             0x38
#define TAS5825M_REG_GLOBAL_FAULT1      0x71
#define TAS5825M_REG_GLOBAL_FAULT2      0x72
#define TAS5825M_REG_WARNING            0x73
#define TAS5825M_REG_FAULT_CLEAR        0x78
// Additional registers used by implementation
#define TAS5825M_REG_POWER_STATE        0x68
#define TAS5825M_REG_SAP_CTRL3          0x35
#define TAS5825M_REG_AUTO_MUTE_CTRL     0x50

// Device states for DEVICE_CTRL2 register
#define TAS5825M_STATE_DEEP_SLEEP       0x00
#define TAS5825M_STATE_SLEEP            0x01
#define TAS5825M_STATE_HIZ              0x02
#define TAS5825M_STATE_PLAY             0x03

// GPIO function selections
#define TAS5825M_GPIO_FUNC_FAULTZ       0x0B
#define TAS5825M_GPIO_FUNC_WARNZ        0x08

class TAS5825MSensor : public I2CSensor {
public:
	explicit TAS5825MSensor(uint8_t i2c_address = TAS5825M_I2C_ADDR);
	~TAS5825MSensor() override = default;

	uint8_t addr() const override;
	std::string name() const override;

	bool init() override;
	bool init(i2c_master_bus_handle_t bus_handle) override;
	void poll() override;
	bool isInitialized() const override;

	bool hasInterruptTriggered() override { return false; }
	void clearInterruptFlag() override {}

private:
	esp_err_t writeReg(uint8_t reg, uint8_t val);
	esp_err_t readReg(uint8_t reg, uint8_t &val);
	bool validateFinalState();

	uint8_t _i2c_addr;
	bool _initialized{false};
	int32_t _last_volume{-1};
	// Change-tracking for reduced logging noise
	bool _poll_logged_once{false};
	uint8_t _last_clk{0xFF};
	uint8_t _last_fs_mon{0xFF};
	uint16_t _last_bclk_per_lrclk_ratio{0xFFFF};
	uint8_t _last_power_state{0xFF};
};
