#include "WifiConfig.h"
#include "cJSON.h"
#include "esp_log.h"
#include <cstring>

namespace config {

static const char* TAG = "WifiConfig";

WifiConfig::WifiConfig() {
    descriptors_.push_back({"ssid", ConfigValueType::String, nullptr, true});
    descriptors_.push_back({"password", ConfigValueType::String, nullptr, true});
    descriptors_.push_back({"mqtt_broker", ConfigValueType::String, nullptr, true});
}

const char* WifiConfig::name() const {
    return "wifi";
}

const std::vector<ConfigurationValueDescriptor>& WifiConfig::descriptors() const {
    return descriptors_;
}

esp_err_t WifiConfig::apply_update(const char* key, const char* value_str) {
    if (key == nullptr) return ESP_ERR_INVALID_ARG;

    if (strcmp(key, "ssid") == 0) {
        ssid_.assign(value_str ? value_str : "");
        ssid_set_ = (value_str != nullptr);
        return ESP_OK;
    }
    if (strcmp(key, "password") == 0) {
        password_.assign(value_str ? value_str : "");
        password_set_ = (value_str != nullptr);
        return ESP_OK;
    }
    if (strcmp(key, "mqtt_broker") == 0) {
        mqtt_broker_.assign(value_str ? value_str : "");
        mqtt_broker_set_ = (value_str != nullptr);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Unknown key '%s'", key);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t WifiConfig::to_json(cJSON* root_object) const {
    if (!root_object) return ESP_ERR_INVALID_ARG;

    cJSON* wifi_obj = cJSON_CreateObject();
    int added = 0;
    if (has_ssid()) {
        cJSON_AddStringToObject(wifi_obj, "ssid", ssid_.c_str());
        added++;
    }
    if (has_password()) {
        cJSON_AddStringToObject(wifi_obj, "password", password_.c_str());
        added++;
    }
    if (has_mqtt_broker()) {
        cJSON_AddStringToObject(wifi_obj, "mqtt_broker", mqtt_broker_.c_str());
        added++;
    }
    if (added > 0) {
        cJSON_AddItemToObject(root_object, name(), wifi_obj);
    } else {
        cJSON_Delete(wifi_obj);
    }
    return ESP_OK;
}

} // namespace config


