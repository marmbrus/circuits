#include "ads1115_sensor.h"
#include "i2c_master_ext.h"
#include "esp_log.h"
#include <cstring>
#include <string>
#include "ConfigurationManager.h"
#include "A2DConfig.h"

static const char *TAG = "ADS1115Sensor";

ADS1115Sensor::ADS1115Sensor(uint8_t i2c_address)
	: I2CSensor(nullptr),
	  _i2c_addr(i2c_address),
	  _initialized(false) {
	for (int i = 0; i < 4; ++i) _channel_tags[i] = nullptr;
	ESP_LOGD(TAG, "Constructed with addr=0x%02X", _i2c_addr);
}

ADS1115Sensor::~ADS1115Sensor() {
	for (int i = 0; i < 4; ++i) {
		if (_channel_tags[i] != nullptr) {
			free_tag_collection(_channel_tags[i]);
			_channel_tags[i] = nullptr;
		}
	}
}

uint8_t ADS1115Sensor::addr() const {
	return _i2c_addr;
}

std::string ADS1115Sensor::name() const {
	char buf[32];
	snprintf(buf, sizeof(buf), "ADS1115@0x%02X", _i2c_addr);
	return std::string(buf);
}

int ADS1115Sensor::index() const {
	if (_i2c_addr < 0x48 || _i2c_addr > 0x4B) return -1;
	return (int)(_i2c_addr - 0x48) + 1; // 0x48->1 .. 0x4B->4
}

std::string ADS1115Sensor::config_module_name() const {
	int idx = index();
	if (idx < 1) return std::string();
	char buf[16];
	snprintf(buf, sizeof(buf), "a2d%d", idx);
	return std::string(buf);
}

bool ADS1115Sensor::init() {
	ESP_LOGE(TAG, "Invalid init() without bus handle. Use init(bus_handle).");
	return false;
}

bool ADS1115Sensor::init(i2c_master_bus_handle_t bus_handle) {
	if (_initialized) {
		ESP_LOGW(TAG, "Already initialized");
		return true;
	}
	if (bus_handle == nullptr) {
		ESP_LOGE(TAG, "Null bus handle");
		return false;
	}

	_bus_handle = bus_handle;

	ESP_LOGI(TAG, "Initializing ADS1115 at 0x%02X", _i2c_addr);

	// Configure device
	i2c_device_config_t dev_cfg = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = _i2c_addr,
		.scl_speed_hz = 400000,
	};

	esp_err_t ret = i2c_master_bus_add_device(_bus_handle, &dev_cfg, &_dev_handle);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to add ADS1115 device: %s", esp_err_to_name(ret));
		return false;
	}

	// Basic sanity read: attempt to read config register (does not have fixed ID reg)
	uint16_t cfg = 0;
	ret = readRegister(REG_CONFIG, cfg);
	if (ret != ESP_OK) {
		ESP_LOGW(TAG, "Failed to read config reg on init, continuing: %s", esp_err_to_name(ret));
	}
	ESP_LOGI(TAG, "Initial CONFIG=0x%04X", cfg);

	_initialized = true;

	// Create per-channel tag collections, seeded with device tags
	for (int ch = 0; ch < 4; ++ch) {
		_channel_tags[ch] = create_tag_collection();
		if (_channel_tags[ch] == nullptr) {
			ESP_LOGE(TAG, "Failed to create tag collection for ch%d", ch + 1);
			return false;
		}
		add_tag_to_collection(_channel_tags[ch], "type", "ads1115");
		char addr_buf[8];
		snprintf(addr_buf, sizeof(addr_buf), "0x%02X", _i2c_addr);
		add_tag_to_collection(_channel_tags[ch], "addr", addr_buf);
		char ch_buf[16];
		snprintf(ch_buf, sizeof(ch_buf), "%d", ch + 1);
		add_tag_to_collection(_channel_tags[ch], "channel", ch_buf);
	}

	// Kick an initial poll
	poll();
	return true;
}

bool ADS1115Sensor::isInitialized() const {
	return _initialized;
}

// Writes 16-bit value in big-endian as required by ADS1115
esp_err_t ADS1115Sensor::writeRegister(uint8_t reg, uint16_t value_be) {
	if (_dev_handle == nullptr) {
		ESP_LOGW(TAG, "writeRegister with null device handle");
		return ESP_ERR_INVALID_STATE;
	}
	uint8_t tx[3];
	tx[0] = reg;
	tx[1] = (uint8_t)((value_be >> 8) & 0xFF);
	tx[2] = (uint8_t)(value_be & 0xFF);
	esp_err_t ret = i2c_master_transmit(_dev_handle, tx, sizeof(tx), I2C_XFR_TIMEOUT_MS);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "I2C write reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
	}
	ESP_LOGD(TAG, "WR reg 0x%02X = 0x%04X", reg, value_be);
	return ret;
}

// Reads 16-bit big-endian value from register
esp_err_t ADS1115Sensor::readRegister(uint8_t reg, uint16_t &value_be) {
	if (_dev_handle == nullptr) {
		ESP_LOGW(TAG, "readRegister with null device handle");
		return ESP_ERR_INVALID_STATE;
	}
	uint8_t rx[2] = {0, 0};
	esp_err_t ret = i2c_master_transmit_receive(_dev_handle, &reg, 1, rx, 2, I2C_XFR_TIMEOUT_MS);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "I2C read reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
		return ret;
	}
	value_be = ((uint16_t)rx[0] << 8) | rx[1];
	ESP_LOGD(TAG, "RD reg 0x%02X -> 0x%04X", reg, value_be);
	return ESP_OK;
}

void ADS1115Sensor::mapGainToPgaAndFs(const char* gain_str, uint16_t &pga_bits, float &fs_volts) const {
	// Defaults
	pga_bits = CFG_PGA_4_096V;
	fs_volts = 4.096f;

	if (!gain_str || *gain_str == '\0' || strcasecmp(gain_str, "FULL") == 0) {
		return; // keep defaults
	}
	if (strcasecmp(gain_str, "FSR_6V144") == 0) { pga_bits = CFG_PGA_6_144V; fs_volts = 6.144f; return; }
	if (strcasecmp(gain_str, "FSR_4V096") == 0) { pga_bits = CFG_PGA_4_096V; fs_volts = 4.096f; return; }
	if (strcasecmp(gain_str, "FSR_2V048") == 0) { pga_bits = CFG_PGA_2_048V; fs_volts = 2.048f; return; }
	if (strcasecmp(gain_str, "FSR_1V024") == 0) { pga_bits = CFG_PGA_1_024V; fs_volts = 1.024f; return; }
	if (strcasecmp(gain_str, "FSR_0V512") == 0) { pga_bits = CFG_PGA_0_512V; fs_volts = 0.512f; return; }
	if (strcasecmp(gain_str, "FSR_0V256") == 0) { pga_bits = CFG_PGA_0_256V; fs_volts = 0.256f; return; }
}

void ADS1115Sensor::poll() {
	if (!_initialized) {
		ESP_LOGW(TAG, "poll before init");
		return;
	}

	// Read all 4 single-ended channels in sequence; report as metrics
	static const char* METRIC_NAME = "volts";
	const uint16_t mux_opts[4] = { CFG_MUX_AIN0_GND, CFG_MUX_AIN1_GND, CFG_MUX_AIN2_GND, CFG_MUX_AIN3_GND };

	// Select A2D configuration module for this device address
	config::A2DConfig* mod = nullptr;
	switch (_i2c_addr) {
		case 0x48: mod = &config::GetConfigurationManager().a2d1(); break;
		case 0x49: mod = &config::GetConfigurationManager().a2d2(); break;
		case 0x4A: mod = &config::GetConfigurationManager().a2d3(); break;
		case 0x4B: mod = &config::GetConfigurationManager().a2d4(); break;
		default: break;
	}

	for (int ch = 0; ch < 4; ++ch) {
		bool channel_enabled = true;
		const char* gain_str = nullptr;
		std::string sensor_str;
		std::string name_str;
		if (mod != nullptr) {
			const config::A2DChannelConfig& ccfg = mod->channel_config(ch + 1);
			if (ccfg.enabled_set) channel_enabled = ccfg.enabled;
			if (ccfg.gain_set) gain_str = ccfg.gain.c_str();
			if (ccfg.sensor_set) sensor_str = ccfg.sensor;
			if (ccfg.name_set) name_str = ccfg.name;
		}
		if (!channel_enabled) {
			continue;
		}

		uint16_t pga_bits; float fs;
		mapGainToPgaAndFs(gain_str, pga_bits, fs);

		int16_t raw = 0;
		uint16_t raw_be = 0;
		for (int attempt = 0; attempt < 2; ++attempt) {
			uint16_t cfg = (uint16_t)(CFG_OS_SINGLE | mux_opts[ch] | pga_bits | CFG_MODE_SINGLE | CFG_DR_128SPS | CFG_COMP_DISABLED);
			esp_err_t ret = writeRegister(REG_CONFIG, cfg);
			if (ret != ESP_OK) {
				ESP_LOGW(TAG, "Failed to start conversion on ch%d", ch + 1);
				break;
			}

			// Wait for conversion to complete based on DR (128SPS ~7.8ms). Use a conservative 10ms.
			vTaskDelay(pdMS_TO_TICKS(10));

			raw_be = 0;
			ret = readRegister(REG_CONVERSION, raw_be);
			if (ret != ESP_OK) {
				ESP_LOGW(TAG, "Failed to read conversion on ch%d", ch + 1);
				break;
			}
			raw = (int16_t)raw_be;
			// Discard first result after MUX change to reduce residual from previous channel
			if (attempt == 0) {
				continue;
			}
		}

		float volts = (float)raw / 32768.0f * fs; // scale signed value to volts

		ESP_LOGD(TAG, "addr=0x%02X ch=%d raw=0x%04X(%d) -> %.6f V", _i2c_addr, ch + 1, raw_be, raw, volts);

		// Update optional name tag on this channel's tag collection
		if (!name_str.empty()) {
			add_tag_to_collection(_channel_tags[ch], "name", name_str.c_str());
		} else {
			remove_tag_from_collection(_channel_tags[ch], "name");
		}

		report_metric(METRIC_NAME, volts, _channel_tags[ch]);

		// Derived metrics for configured sensors
		if (!sensor_str.empty()) {
			if (sensor_str == "RSUV") {
				float kpa = (volts - 0.5f) / 0.0426f;
				report_metric("kpa", kpa, _channel_tags[ch]);
			} else if (sensor_str == "BTS7002") {
				const float sense_resistance_ohms = 1500.0f;
				const float kILIS = 22900.0f;
				float i_is_amps = volts / sense_resistance_ohms;
				float i_load_amps = i_is_amps * kILIS;
				report_metric("amps", i_load_amps, _channel_tags[ch]);
			}
		}
	}
}


