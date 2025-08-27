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
	return ESP_ERR_NOT_FOUND;
}

esp_err_t SpeakerConfig::to_json(struct cJSON* root_object) const {
	if (!root_object) return ESP_ERR_INVALID_ARG;
	cJSON* obj = cJSON_CreateObject();
	if (!obj) return ESP_ERR_NO_MEM;
	if (sdin_set_) cJSON_AddNumberToObject(obj, "sdin", (double)sdin_gpio_);
	if (sclk_set_) cJSON_AddNumberToObject(obj, "sclk", (double)sclk_gpio_);
	if (lrclk_set_) cJSON_AddNumberToObject(obj, "lrclk", (double)lrclk_gpio_);
	cJSON_AddItemToObject(root_object, name(), obj);
	return ESP_OK;
}

} // namespace config
