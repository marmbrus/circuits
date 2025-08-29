#include "SpeakerConfig.h"
#include "cJSON.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

namespace config {

static const char* TAG = "SpeakerConfig";

SpeakerConfig::SpeakerConfig() {
	descriptors_.push_back({"sdin", ConfigValueType::I32, nullptr, true});
	descriptors_.push_back({"sclk", ConfigValueType::I32, nullptr, true});
	descriptors_.push_back({"lrclk", ConfigValueType::I32, nullptr, true});
	// Non-persistent runtime controls
	descriptors_.push_back({"sine", ConfigValueType::I32, nullptr, false});
	descriptors_.push_back({"url", ConfigValueType::String, nullptr, false});
	descriptors_.push_back({"volume", ConfigValueType::I32, nullptr, false});
}

const char* SpeakerConfig::name() const {
	return "speaker";
}

const std::vector<ConfigurationValueDescriptor>& SpeakerConfig::descriptors() const {
	return descriptors_;
}

esp_err_t SpeakerConfig::apply_update(const char* key, const char* value_str) {
	if (!key) return ESP_ERR_INVALID_ARG;
	bool clear = (value_str == nullptr || value_str[0] == '\0');
	if (strcmp(key, "sdin") == 0) {
		if (clear) { sdin_set_ = false; sdin_gpio_ = 0; return ESP_OK; }
		char* end = nullptr; long v = strtol(value_str, &end, 10);
		if (end == value_str) return ESP_ERR_INVALID_ARG;
		sdin_gpio_ = (uint32_t)v; sdin_set_ = true; return ESP_OK;
	}
	if (strcmp(key, "sclk") == 0) {
		if (clear) { sclk_set_ = false; sclk_gpio_ = 0; return ESP_OK; }
		char* end = nullptr; long v = strtol(value_str, &end, 10);
		if (end == value_str) return ESP_ERR_INVALID_ARG;
		sclk_gpio_ = (uint32_t)v; sclk_set_ = true; return ESP_OK;
	}
	if (strcmp(key, "lrclk") == 0) {
		if (clear) { lrclk_set_ = false; lrclk_gpio_ = 0; return ESP_OK; }
		char* end = nullptr; long v = strtol(value_str, &end, 10);
		if (end == value_str) return ESP_ERR_INVALID_ARG;
		lrclk_gpio_ = (uint32_t)v; lrclk_set_ = true; return ESP_OK;
	}
	if (strcmp(key, "sine") == 0) {
		if (clear) { sine_set_ = false; sine_hz_ = 0; return ESP_OK; }
		char* end = nullptr; long v = strtol(value_str, &end, 10);
		if (end == value_str) return ESP_ERR_INVALID_ARG;
		sine_hz_ = (int32_t)v; sine_set_ = true; return ESP_OK;
	}
	if (strcmp(key, "url") == 0) {
		if (clear) { url_set_ = false; url_.clear(); return ESP_OK; }
		url_ = value_str; url_set_ = true; return ESP_OK;
	}
	if (strcmp(key, "volume") == 0) {
		if (clear) { volume_set_ = false; volume_ = -1; return ESP_OK; }
		char* end = nullptr; long v = strtol(value_str, &end, 10);
		if (end == value_str) return ESP_ERR_INVALID_ARG;
		volume_ = (int32_t)v; volume_set_ = true; return ESP_OK;
	}
	return ESP_ERR_NOT_FOUND;
}

esp_err_t SpeakerConfig::to_json(struct cJSON* root_object) const {
	if (!root_object) return ESP_ERR_INVALID_ARG;
	cJSON* obj = cJSON_CreateObject();
	if (!obj) return ESP_ERR_NO_MEM;
	if (sdin_set_) cJSON_AddNumberToObject(obj, "sdin", (double)sdin_gpio_);
	if (sclk_set_) cJSON_AddNumberToObject(obj, "sclk", (double)sclk_gpio_);
	if (lrclk_set_) cJSON_AddNumberToObject(obj, "lrclk", (double)lrclk_gpio_);
	if (sine_set_) cJSON_AddNumberToObject(obj, "sine", (double)sine_hz_);
	if (url_set_) cJSON_AddStringToObject(obj, "url", url_.c_str());
	if (volume_set_) cJSON_AddNumberToObject(obj, "volume", (double)volume_);
	cJSON_AddItemToObject(root_object, name(), obj);
	return ESP_OK;
}

} // namespace config
