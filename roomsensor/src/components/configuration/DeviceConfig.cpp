#include "DeviceConfig.h"
#include "cJSON.h"
#include <string.h>

namespace config {

const std::vector<ConfigurationValueDescriptor> DeviceConfig::descriptors_ = {
    { "type", ConfigValueType::String, nullptr, true },
};

DeviceConfig::DeviceConfig() {}
DeviceConfig::~DeviceConfig() {}

const std::vector<ConfigurationValueDescriptor>& DeviceConfig::descriptors() const {
    return descriptors_;
}

esp_err_t DeviceConfig::apply_update(const char* key, const char* value_str) {
    if (strcmp(key, "type") == 0) {
        type_ = value_str ? value_str : "";
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t DeviceConfig::to_json(cJSON* root_object) const {
    cJSON* module_object = cJSON_CreateObject();
    cJSON_AddStringToObject(module_object, "type", type_.c_str());
    cJSON_AddItemToObject(root_object, name(), module_object);
    return ESP_OK;
}

} // namespace config
