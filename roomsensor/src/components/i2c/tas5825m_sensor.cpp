#include "tas5825m_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "ConfigurationManager.h"
#include "SpeakerConfig.h"

static const char *TAG_TAS = "TAS5825M";

TAS5825MSensor::TAS5825MSensor(uint8_t i2c_address)
	: I2CSensor(nullptr), _i2c_addr(i2c_address) {}

uint8_t TAS5825MSensor::addr() const { return _i2c_addr; }
std::string TAS5825MSensor::name() const { return std::string("TAS5825M"); }

bool TAS5825MSensor::init() { return false; }

esp_err_t TAS5825MSensor::writeReg(uint8_t reg, uint8_t val) {
	uint8_t buf[2] = {reg, val};
	return i2c_master_transmit(_dev_handle, buf, sizeof(buf), -1);
}

esp_err_t TAS5825MSensor::readReg(uint8_t reg, uint8_t &val) {
	return i2c_master_transmit_receive(_dev_handle, &reg, 1, &val, 1, -1);
}

bool TAS5825MSensor::validateFinalState() {
	uint8_t v = 0;
	if (readReg(TAS5825M_REG_POWER_STATE, v) != ESP_OK) return false;
	ESP_LOGI(TAG_TAS, "POWER_STATE(0x68)=0x%02X", v);
	uint8_t clk, f1, f2, warn;
	if (readReg(TAS5825M_REG_CLKDET_STATUS, clk) != ESP_OK) return false;
	ESP_LOGI(TAG_TAS, "CLKDET_STATUS(0x39)=0x%02X", clk);
	if (readReg(TAS5825M_REG_GLOBAL_FAULT1, f1) != ESP_OK) return false;
	ESP_LOGI(TAG_TAS, "GLOBAL_FAULT1(0x71)=0x%02X", f1);
	if (readReg(TAS5825M_REG_GLOBAL_FAULT2, f2) != ESP_OK) return false;
	ESP_LOGI(TAG_TAS, "GLOBAL_FAULT2(0x72)=0x%02X", f2);
	if (readReg(TAS5825M_REG_WARNING, warn) != ESP_OK) return false;
	ESP_LOGI(TAG_TAS, "WARNING(0x73)=0x%02X", warn);
	return true;
}

bool TAS5825MSensor::init(i2c_master_bus_handle_t bus_handle) {
	ESP_LOGI(TAG_TAS, "Initializing TAS5825M at 0x%02X", _i2c_addr);
	_bus_handle = bus_handle;

	i2c_device_config_t dev_cfg = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = _i2c_addr,
		.scl_speed_hz = 400000,
	};
	if (i2c_master_bus_add_device(_bus_handle, &dev_cfg, &_dev_handle) != ESP_OK) {
		ESP_LOGE(TAG_TAS, "Failed to add I2C device");
		return false;
	}

	auto chk = [&](esp_err_t e, const char *msg) -> bool {
		if (e != ESP_OK) {
			ESP_LOGE(TAG_TAS, "%s: %s", msg, esp_err_to_name(e));
			return false;
		}
		return true;
	};

	// Full reset sequence
	if (!chk(writeReg(TAS5825M_REG_RESET_CTRL, 0x01), "reset regs")) return false;
	vTaskDelay(pdMS_TO_TICKS(10));
	if (!chk(writeReg(TAS5825M_REG_RESET_CTRL, 0x10), "core reset")) return false;
	vTaskDelay(pdMS_TO_TICKS(10));

	// Output mode default
	if (!chk(writeReg(TAS5825M_REG_DEVICE_CTRL1, 0x00), "device ctrl1")) return false;

	// Deep sleep
	if (!chk(writeReg(TAS5825M_REG_DEVICE_CTRL2, TAS5825M_STATE_DEEP_SLEEP), "device ctrl2 deep sleep")) return false;
	vTaskDelay(pdMS_TO_TICKS(5));

	// I2S: 16-bit I2S format
	if (!chk(writeReg(TAS5825M_REG_SAP_CTRL1, 0x00), "sap ctrl1")) return false;

	// HiZ
	if (!chk(writeReg(TAS5825M_REG_DEVICE_CTRL2, TAS5825M_STATE_HIZ), "device ctrl2 hiz")) return false;
	vTaskDelay(pdMS_TO_TICKS(5));

	// DSP ROM mode
	if (!chk(writeReg(TAS5825M_REG_DSP_PGM_MODE, 0x01), "dsp rom mode")) return false;
	vTaskDelay(pdMS_TO_TICKS(5));

	// GPIO config (FAULTZ/WARNZ)
	if (!chk(writeReg(TAS5825M_REG_GPIO_CTRL, 0x06), "gpio ctrl")) return false;
	if (!chk(writeReg(TAS5825M_REG_GPIO1_SEL, TAS5825M_GPIO_FUNC_FAULTZ), "gpio1 sel")) return false;
	if (!chk(writeReg(TAS5825M_REG_GPIO2_SEL, TAS5825M_GPIO_FUNC_WARNZ), "gpio2 sel")) return false;

	// Volume and auto-mute
	if (!chk(writeReg(TAS5825M_REG_DIG_VOL, 150), "dig vol")) return false;
	if (!chk(writeReg(TAS5825M_REG_AUTO_MUTE_CTRL, 0x00), "auto mute ctrl")) return false;

	// Routing
	if (!chk(writeReg(TAS5825M_REG_SAP_CTRL3, 0x11), "sap ctrl3")) return false;

	// Clear faults
	if (!chk(writeReg(TAS5825M_REG_FAULT_CLEAR, 0x80), "fault clear")) return false;
	vTaskDelay(pdMS_TO_TICKS(5));

	// Play
	if (!chk(writeReg(TAS5825M_REG_DEVICE_CTRL2, TAS5825M_STATE_PLAY), "device ctrl2 play")) return false;
	vTaskDelay(pdMS_TO_TICKS(10));

	_initialized = validateFinalState();
	ESP_LOGI(TAG_TAS, "TAS5825M init %s", _initialized ? "OK" : "FAILED");
	return _initialized;
}

void TAS5825MSensor::poll() {
	if (!_initialized) return;

	uint8_t clk = 0, fs_mon = 0, bck_mon = 0, pwr = 0, f1 = 0, f2 = 0, warn = 0;
	if (readReg(TAS5825M_REG_CLKDET_STATUS, clk) == ESP_OK) {
		ESP_LOGI(TAG_TAS, "CLKDET_STATUS=0x%02X%s%s%s%s%s%s",
				 clk,
				 (clk & 0x01) ? " Sampling rate invalid (FS error)" : "",
				 (clk & 0x02) ? " Serial clock ratio invalid" : "",
				 (clk & 0x04) ? " Serial clock missing" : "",
				 (clk & 0x08) ? " PLL locked" : " PLL unlocked",
				 (clk & 0x10) ? " PLL overrate" : "",
				 (clk & 0x20) ? " Serial clock over/under rate" : "");
	}

	if (readReg(TAS5825M_REG_FS_MON, fs_mon) == ESP_OK) {
		uint8_t fs_code = fs_mon & 0x0F;
		const char *fs_str = "Unknown";
		switch (fs_code) {
			case 0x09: fs_str = "48kHz"; break;
			case 0x0B: fs_str = "96kHz"; break;
			case 0x0D: fs_str = "192kHz"; break;
			case 0x08: fs_str = "44.1kHz"; break;
			case 0x00: fs_str = "FS_ERROR"; break;
			default: break;
		}
		ESP_LOGI(TAG_TAS, "FS_MON=0x%02X (sample_rate=%s)", fs_mon, fs_str);
	}

	if (readReg(TAS5825M_REG_BCK_MON, bck_mon) == ESP_OK && readReg(TAS5825M_REG_FS_MON, fs_mon) == ESP_OK) {
		uint16_t bclk_per_lrclk_ratio = ((fs_mon & 0x30) << 4) | bck_mon;
		ESP_LOGI(TAG_TAS, "BCK_MON=0x%02X (bclk_per_lrclk_ratio=%u)", bck_mon, (unsigned)bclk_per_lrclk_ratio);
	}

	if (readReg(TAS5825M_REG_POWER_STATE, pwr) == ESP_OK) {
		const char *state_str = "Reserved";
		switch (pwr & 0xFF) {
			case 0x00: state_str = "Deep sleep"; break;
			case 0x01: state_str = "Sleep"; break;
			case 0x02: state_str = "HIZ"; break;
			case 0x03: state_str = "Play"; break;
			default: state_str = "Reserved"; break;
		}
		ESP_LOGI(TAG_TAS, "POWER_STATE=0x%02X (%s)", pwr, state_str);
	}

	if (readReg(TAS5825M_REG_GLOBAL_FAULT1, f1) == ESP_OK &&
		readReg(TAS5825M_REG_GLOBAL_FAULT2, f2) == ESP_OK &&
		readReg(TAS5825M_REG_WARNING, warn) == ESP_OK) {
		ESP_LOGI(TAG_TAS, "GLOBAL_FAULT1=0x%02X%s%s%s%s%s%s",
			f1,
			(f1 & 0x80) ? " OTP CRC error" : "",
			(f1 & 0x40) ? " BQ write error" : "",
			(f1 & 0x20) ? " EEPROM load error" : "",
			(f1 & 0x04) ? " Clock fault (latched)" : "",
			(f1 & 0x02) ? " PVDD over-voltage" : "",
			(f1 & 0x01) ? " PVDD under-voltage" : "");

		ESP_LOGI(TAG_TAS, "GLOBAL_FAULT2=0x%02X%s%s%s",
			f2,
			(f2 & 0x04) ? " Right channel overcurrent fault (cycle-by-cycle)" : "",
			(f2 & 0x02) ? " Left channel overcurrent fault (cycle-by-cycle)" : "",
			(f2 & 0x01) ? " Over-temperature shutdown" : "");

		ESP_LOGI(TAG_TAS, "WARNING=0x%02X%s%s%s%s%s%s",
			warn,
			(warn & 0x20) ? " Left channel overcurrent warning (cycle-by-cycle)" : "",
			(warn & 0x10) ? " Right channel overcurrent warning (cycle-by-cycle)" : "",
			(warn & 0x08) ? " Over-temperature warning level 4 (146°C)" : "",
			(warn & 0x04) ? " Over-temperature warning level 3 (134°C)" : "",
			(warn & 0x02) ? " Over-temperature warning level 2 (122°C)" : "",
			(warn & 0x01) ? " Over-temperature warning level 1 (112°C)" : "");

		if (f1 || f2 || warn) {
			if (writeReg(TAS5825M_REG_FAULT_CLEAR, 0x80) == ESP_OK) {
				ESP_LOGI(TAG_TAS, "Cleared fault and warning latches (FAULT_CLEAR=0x80)");
			} else {
				ESP_LOGW(TAG_TAS, "Failed to clear fault latches");
			}
		}
	}

	// Apply volume configuration if present (0-255 scale expected)
	{
		auto &sp = config::GetConfigurationManager().speaker();
		if (sp.has_volume()) {
			int v = sp.volume();
			if (v < 0) v = 0;
			if (v > 255) v = 255;
			if (v != _last_volume) {
				_last_volume = v;
				writeReg(TAS5825M_REG_DIG_VOL, (uint8_t)v);
				ESP_LOGI(TAG_TAS, "Set volume 0x%02X", (unsigned)v);
			}
		}
	}
}

bool TAS5825MSensor::isInitialized() const { return _initialized; }
