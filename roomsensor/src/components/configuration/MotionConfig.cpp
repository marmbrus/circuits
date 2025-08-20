#include "MotionConfig.h"
#include "cJSON.h"
#include <cstring>

namespace config {

MotionConfig::MotionConfig() {
    // Single persisted, nullable integer value: gpio
    descriptors_.push_back({"gpio", ConfigValueType::I32, nullptr, true});
}

const char* MotionConfig::name() const {
    return "motion";
}

const std::vector<ConfigurationValueDescriptor>& MotionConfig::descriptors() const {
    return descriptors_;
}

esp_err_t MotionConfig::apply_update(const char* key, const char* value_str) {
    if (key == nullptr) return ESP_ERR_INVALID_ARG;
    if (strcmp(key, "gpio") == 0) {
        // Nullable: if no value provided, clear the setting
        if (value_str == nullptr || value_str[0] == '\0') {
            gpio_set_ = false;
            gpio_ = -1;
            return ESP_OK;
        }
        gpio_set_ = true;
        gpio_ = atoi(value_str);
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t MotionConfig::to_json(cJSON* root_object) const {
    if (!root_object) return ESP_ERR_INVALID_ARG;
    // Only include module if gpio is set
    if (!gpio_set_) return ESP_OK;
    cJSON* obj = cJSON_CreateObject();
    cJSON_AddNumberToObject(obj, "gpio", gpio_);
    cJSON_AddItemToObject(root_object, name(), obj);
    return ESP_OK;
}

} // namespace config



