#include "A2DConfig.h"
#include "cJSON.h"
#include <cstring>
#include "esp_log.h"

namespace config {

static const char* TAG = "A2DConfig";

static bool is_valid_gain(const char* g) {
	if (!g) return false;
	// Allow a conservative set of textual values; ADS1115 supports +/-6.144 to +/-0.256
	return strcmp(g, "FSR_6V144") == 0 || strcmp(g, "FSR_4V096") == 0 || strcmp(g, "FSR_2V048") == 0 ||
	       strcmp(g, "FSR_1V024") == 0 || strcmp(g, "FSR_0V512") == 0 || strcmp(g, "FSR_0V256") == 0 ||
	       strcmp(g, "FULL") == 0; // alias for 5V-ish (we map to 4.096V by default)
}

static bool is_valid_sensor(const char* s) {
	if (!s) return false;
	return strcmp(s, "BTS7002") == 0 || strcmp(s, "RSUV") == 0;
}

A2DConfig::A2DConfig(const char* instance_name) : name_(instance_name ? instance_name : "a2d") {
	// We declare descriptors for channel subkeys ch1..ch4 as blobs of string fields
	// Keys exposed: ch1.enabled, ch1.gain, ch1.sensor, ch1.name ... ch4.*
	for (int ch = 1; ch <= 4; ++ch) {
		char k1[16]; snprintf(k1, sizeof(k1), "ch%d.enabled", ch);
		descriptors_.push_back({strdup(k1), ConfigValueType::String, nullptr, true});
		char k2[12]; snprintf(k2, sizeof(k2), "ch%d.gain", ch);
		descriptors_.push_back({strdup(k2), ConfigValueType::String, nullptr, true});
		char k3[14]; snprintf(k3, sizeof(k3), "ch%d.sensor", ch);
		descriptors_.push_back({strdup(k3), ConfigValueType::String, nullptr, true});
		char k4[12]; snprintf(k4, sizeof(k4), "ch%d.name", ch);
		descriptors_.push_back({strdup(k4), ConfigValueType::String, nullptr, true});
	}
}

const char* A2DConfig::name() const { return name_.c_str(); }

const std::vector<ConfigurationValueDescriptor>& A2DConfig::descriptors() const { return descriptors_; }

esp_err_t A2DConfig::apply_update(const char* key, const char* value_str) {
	if (!key) return ESP_ERR_INVALID_ARG;

	// Expect keys like ch1.enabled, ch1.gain, ch1.sensor, ch1.name
	if (strncmp(key, "ch", 2) != 0) return ESP_ERR_NOT_FOUND;
	int ch = 0;
	char field[16] = {0};
	if (sscanf(key, "ch%d.%15s", &ch, field) != 2) return ESP_ERR_INVALID_ARG;
	if (ch < 1 || ch > 4) return ESP_ERR_INVALID_ARG;
	A2DChannelConfig &cfg = channels_[ch - 1];

	if (strcmp(field, "enabled") == 0) {
		// interpret value as bool ("true"/"false"/"1"/"0")
		bool v = false;
		if (value_str) {
			if (strcasecmp(value_str, "true") == 0 || strcmp(value_str, "1") == 0) v = true;
			else if (strcasecmp(value_str, "false") == 0 || strcmp(value_str, "0") == 0) v = false;
			else {
				ESP_LOGW(TAG, "%s: invalid boolean for %s: '%s'", name_.c_str(), key, value_str);
				return ESP_ERR_INVALID_ARG;
			}
		}
		cfg.enabled = v;
		cfg.enabled_set = true;
		return ESP_OK;
	}
	if (strcmp(field, "gain") == 0) {
		if (value_str && is_valid_gain(value_str)) {
			cfg.gain = value_str;
			cfg.gain_set = true;
			return ESP_OK;
		}
		ESP_LOGW(TAG, "%s: invalid gain for %s: '%s'", name_.c_str(), key, value_str ? value_str : "");
		return ESP_ERR_INVALID_ARG;
	}
	if (strcmp(field, "sensor") == 0) {
		if (value_str == nullptr || *value_str == '\0') {
			cfg.sensor.clear();
			cfg.sensor_set = false; // empty clears explicit setting
			return ESP_OK;
		}
		if (is_valid_sensor(value_str)) {
			cfg.sensor = value_str;
			cfg.sensor_set = true;
			return ESP_OK;
		}
		ESP_LOGW(TAG, "%s: invalid sensor for %s: '%s'", name_.c_str(), key, value_str);
		return ESP_ERR_INVALID_ARG;
	}
	if (strcmp(field, "name") == 0) {
		if (value_str == nullptr || *value_str == '\0') {
			cfg.name.clear();
			cfg.name_set = false;
			return ESP_OK;
		}
		cfg.name = value_str;
		cfg.name_set = true;
		return ESP_OK;
	}

	ESP_LOGW(TAG, "%s: unknown config key '%s'", name_.c_str(), key);
	return ESP_ERR_NOT_FOUND;
}

esp_err_t A2DConfig::to_json(cJSON* root_object) const {
	if (!root_object) return ESP_ERR_INVALID_ARG;
	// Only include module if at least one channel has an explicitly set value
	bool any = false;
	for (const auto &ch : channels_) if (ch.any_set()) { any = true; break; }
	if (!any) return ESP_OK;

	cJSON* obj = cJSON_CreateObject();
	for (int i = 0; i < 4; ++i) {
		const A2DChannelConfig &ch = channels_[i];
		if (!ch.any_set()) continue; // omit channels with pure defaults
		char ch_key[8]; snprintf(ch_key, sizeof(ch_key), "ch%d", i + 1);
		cJSON* ch_obj = cJSON_CreateObject();
		if (ch.enabled_set) cJSON_AddBoolToObject(ch_obj, "enabled", ch.enabled);
		if (ch.gain_set) cJSON_AddStringToObject(ch_obj, "gain", ch.gain.c_str());
		if (ch.sensor_set) cJSON_AddStringToObject(ch_obj, "sensor", ch.sensor.c_str());
		if (ch.name_set)   cJSON_AddStringToObject(ch_obj, "name", ch.name.c_str());
		cJSON_AddItemToObject(obj, ch_key, ch_obj);
	}
	cJSON_AddItemToObject(root_object, name(), obj);
	return ESP_OK;
}

const A2DChannelConfig& A2DConfig::channel_config(int channel) const {
	static A2DChannelConfig dummy; // defaults
	if (channel < 1 || channel > 4) return dummy;
	return channels_[channel - 1];
}

} // namespace config


