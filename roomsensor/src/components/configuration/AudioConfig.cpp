#include "AudioConfig.h"
#include "cJSON.h"
#include <cstdlib>
#include <cstring>

namespace config {

AudioConfig::AudioConfig() {
    descriptors_.push_back({"bclkGPIO", ConfigValueType::I32, nullptr, true});
    descriptors_.push_back({"wsGPIO", ConfigValueType::I32, nullptr, true});
    descriptors_.push_back({"doutGPIO", ConfigValueType::I32, nullptr, true});
    descriptors_.push_back({"dinGPIO", ConfigValueType::I32, nullptr, true});
    descriptors_.push_back({"enableGPIO", ConfigValueType::I32, nullptr, true});

    // Runtime (non-persisted) playback controls
    descriptors_.push_back({"url", ConfigValueType::String, nullptr, false});
    descriptors_.push_back({"volume", ConfigValueType::I32, "50", false});
}

const char* AudioConfig::name() const {
    return "audio";
}

const std::vector<ConfigurationValueDescriptor>& AudioConfig::descriptors() const {
    return descriptors_;
}

esp_err_t AudioConfig::set_gpio(int& field, const char* value_str) {
    // Nullable: if no value provided, clear the setting
    if (value_str == nullptr || value_str[0] == '\0') {
        field = -1;
        return ESP_OK;
    }
    field = atoi(value_str);
    return ESP_OK;
}

esp_err_t AudioConfig::apply_update(const char* key, const char* value_str) {
    if (key == nullptr) return ESP_ERR_INVALID_ARG;

    if (strcmp(key, "bclkGPIO") == 0) return set_gpio(bclk_gpio_, value_str);
    if (strcmp(key, "wsGPIO") == 0) return set_gpio(ws_gpio_, value_str);
    if (strcmp(key, "doutGPIO") == 0) return set_gpio(dout_gpio_, value_str);
    if (strcmp(key, "dinGPIO") == 0) return set_gpio(din_gpio_, value_str);
    if (strcmp(key, "enableGPIO") == 0) return set_gpio(enable_gpio_, value_str);

    if (strcmp(key, "url") == 0) {
        url_ = value_str ? value_str : "";
        return ESP_OK;
    }
    if (strcmp(key, "volume") == 0) {
        if (value_str == nullptr || value_str[0] == '\0') {
            volume_ = 50;
            return ESP_OK;
        }
        int v = atoi(value_str);
        if (v < 0 || v > 100) return ESP_ERR_INVALID_ARG;
        volume_ = v;
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t AudioConfig::to_json(cJSON* root_object) const {
    if (!root_object) return ESP_ERR_INVALID_ARG;
    // Only include module on boards with audio configured
    if (!has_clock_pins()) return ESP_OK;

    cJSON* obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "bclkGPIO", bclk_gpio_);
    cJSON_AddNumberToObject(obj, "wsGPIO", ws_gpio_);
    if (dout_gpio_ >= 0) cJSON_AddNumberToObject(obj, "doutGPIO", dout_gpio_);
    if (din_gpio_ >= 0) cJSON_AddNumberToObject(obj, "dinGPIO", din_gpio_);
    if (enable_gpio_ >= 0) cJSON_AddNumberToObject(obj, "enableGPIO", enable_gpio_);
    if (!url_.empty()) cJSON_AddStringToObject(obj, "url", url_.c_str());
    cJSON_AddNumberToObject(obj, "volume", volume_);
    cJSON_AddItemToObject(root_object, name(), obj);
    return ESP_OK;
}

} // namespace config
