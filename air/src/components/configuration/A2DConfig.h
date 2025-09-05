#pragma once

#include "ConfigurationModule.h"
#include <string>
#include <array>

namespace config {

// MQTT configuration usage
//
// Modules map to ADS1115 addresses as:
// - a2d1 -> 0x48
// - a2d2 -> 0x49
// - a2d3 -> 0x4A
// - a2d4 -> 0x4B
//
// The device subscribes to: sensor/$mac/config/+/+
// So to update a field publish to: sensor/$mac/config/<module>/<key>
// Keys supported per channel: ch1.enabled, ch1.gain, ch1.sensor, ch1.name (repeat for ch2..ch4)
//
// Examples (replace <HOST> and <mac> with your broker and device MAC string):
// - Enable channel 1 on a2d2 (addr 0x49):
//   mosquitto_pub -h <HOST> -t "sensor/<mac>/config/a2d2/ch1.enabled" -m "true"
//
// - Set channel 1 gain to ±4.096V (see valid values below):
//   mosquitto_pub -h <HOST> -t "sensor/<mac>/config/a2d2/ch1.gain" -m "FSR_4V096"
//   Valid gains: FULL, FSR_6V144, FSR_4V096, FSR_2V048, FSR_1V024, FSR_0V512, FSR_0V256
//
// - Interpret channel 1 as RSUV (report kPa in addition to volts):
//   mosquitto_pub -h <HOST> -t "sensor/<mac>/config/a2d2/ch1.sensor" -m "RSUV"
//   RSUV conversion: kPa = (volts - 0.5) / 0.0426
//
// - Interpret channel 1 as BTS7002 (report amps in addition to volts):
//   mosquitto_pub -h <HOST> -t "sensor/<mac>/config/a2d2/ch1.sensor" -m "BTS7002"
//   Notes: IS pin via 1.5kΩ to GND -> I_IS = volts/1500; I_load = I_IS * kILIS (default 5000)
//
// - Set a friendly name for channel 1 (included as metric tag "name" when set):
//   mosquitto_pub -h <HOST> -t "sensor/<mac>/config/a2d2/ch1.name" -m "north_bed_valve"
//
// - Clear sensor interpretation (return to voltage-only):
//   mosquitto_pub -h <HOST> -t "sensor/<mac>/config/a2d2/ch1.sensor" -n
//
// - Clear channel name:
//   mosquitto_pub -h <HOST> -t "sensor/<mac>/config/a2d2/ch1.name" -n

enum class A2DSensorKind {
	None,
	BTS7002,
	RSUV,
};

struct A2DChannelConfig {
	bool enabled = true;
	bool enabled_set = false;

	std::string gain; // textual enum
	bool gain_set = false;

	std::string sensor; // textual enum
	bool sensor_set = false;

	// Optional friendly name; when set, included as a metric tag "name"
	std::string name;
	bool name_set = false;

	bool any_set() const { return enabled_set || gain_set || sensor_set || name_set; }
};

class A2DConfig : public ConfigurationModule {
public:
	explicit A2DConfig(const char* instance_name);

	const char* name() const override;
	const std::vector<ConfigurationValueDescriptor>& descriptors() const override;
	esp_err_t apply_update(const char* key, const char* value_str) override;
	esp_err_t to_json(struct cJSON* root_object) const override;

	// Access per-channel config (1-based channel index: 1..4)
	const A2DChannelConfig& channel_config(int channel) const;

private:
	std::string name_;
	std::vector<ConfigurationValueDescriptor> descriptors_;
	std::array<A2DChannelConfig, 4> channels_;
};

} // namespace config


