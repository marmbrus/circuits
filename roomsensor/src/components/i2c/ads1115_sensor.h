#pragma once

#include "i2c_sensor.h"
#include "esp_err.h"
#include "communication.h"
#include <string>

/**
 * ADS1115 16-bit ADC (4 single-ended channels) I2C sensor wrapper
 */
class ADS1115Sensor : public I2CSensor {
public:
	explicit ADS1115Sensor(uint8_t i2c_address);
	~ADS1115Sensor() override;

	uint8_t addr() const override;
	std::string name() const override;

	bool init() override;
	bool init(i2c_master_bus_handle_t bus_handle) override;
	void poll() override;
	bool isInitialized() const override;

	int index() const override;
	std::string config_module_name() const override;

	bool hasInterruptTriggered() override { return false; }
	void clearInterruptFlag() override {}

private:
	// ADS1115 registers
	static constexpr uint8_t REG_CONVERSION = 0x00;
	static constexpr uint8_t REG_CONFIG     = 0x01;
	static constexpr uint8_t REG_LO_THRESH  = 0x02;
	static constexpr uint8_t REG_HI_THRESH  = 0x03;

	// Config bits
	static constexpr uint16_t CFG_OS_SINGLE      = 0x8000; // bit 15
	static constexpr uint16_t CFG_MUX_AIN0_GND   = 0x4000; // 100 << 12
	static constexpr uint16_t CFG_MUX_AIN1_GND   = 0x5000; // 101 << 12
	static constexpr uint16_t CFG_MUX_AIN2_GND   = 0x6000; // 110 << 12
	static constexpr uint16_t CFG_MUX_AIN3_GND   = 0x7000; // 111 << 12
	// PGA options (bits 11:9)
	static constexpr uint16_t CFG_PGA_6_144V     = 0x0000; // 000 << 9
	static constexpr uint16_t CFG_PGA_4_096V     = 0x0200; // 001 << 9
	static constexpr uint16_t CFG_PGA_2_048V     = 0x0400; // 010 << 9
	static constexpr uint16_t CFG_PGA_1_024V     = 0x0600; // 011 << 9
	static constexpr uint16_t CFG_PGA_0_512V     = 0x0800; // 100 << 9
	static constexpr uint16_t CFG_PGA_0_256V     = 0x0A00; // 101 << 9 (also 110,111)
	static constexpr uint16_t CFG_MODE_SINGLE    = 0x0100; // bit 8
	static constexpr uint16_t CFG_DR_128SPS      = 0x0080; // 100 << 5
	static constexpr uint16_t CFG_COMP_DISABLED  = 0x0003; // COMP_QUE = 11

	// I2C helpers
	esp_err_t writeRegister(uint8_t reg, uint16_t value_be);
	esp_err_t readRegister(uint8_t reg, uint16_t &value_be);

	// Gain mapping
	void mapGainToPgaAndFs(const char* gain_str, uint16_t &pga_bits, float &fs_volts) const;

	// State
	uint8_t _i2c_addr;
	bool _initialized;
	TagCollection* _channel_tags[4];
};
