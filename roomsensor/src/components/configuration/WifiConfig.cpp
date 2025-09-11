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
    descriptors_.push_back({"channel", ConfigValueType::String, nullptr, true});
    // Default loglevel warn (2). Persisted to NVS and applied at runtime.
    descriptors_.push_back({"loglevel", ConfigValueType::I32, "2", true});
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
        // Allow unset (nullptr) to clear
        if (value_str == nullptr) {
            ssid_.clear();
            ssid_set_ = true;
            return ESP_OK;
        }
        size_t len = strlen(value_str);
        if (len == 0 || len > 32) {
            ESP_LOGE(TAG, "Invalid SSID length: %u (must be 1..32)", (unsigned)len);
            return ESP_ERR_INVALID_ARG;
        }
        ssid_.assign(value_str);
        ssid_set_ = true;
        return ESP_OK;
    }
    if (strcmp(key, "password") == 0) {
        // Allow unset (nullptr) to clear
        if (value_str == nullptr) {
            password_.clear();
            password_set_ = true;
            return ESP_OK;
        }
        size_t len = strlen(value_str);
        bool is_hex64 = false;
        if (len == 64) {
            is_hex64 = true;
            for (const char* p = value_str; *p; ++p) {
                char c = *p;
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                    is_hex64 = false;
                    break;
                }
            }
        }
        if (!is_hex64 && (len < 8 || len > 63)) {
            ESP_LOGE(TAG, "Invalid WiFi password length: %u (must be 8..63, or 64 hex)", (unsigned)len);
            return ESP_ERR_INVALID_ARG;
        }
        password_.assign(value_str);
        password_set_ = true;
        return ESP_OK;
    }
    if (strcmp(key, "mqtt_broker") == 0) {
        mqtt_broker_.assign(value_str ? value_str : "");
        mqtt_broker_set_ = (value_str != nullptr);
        return ESP_OK;
    }

    if (strcmp(key, "channel") == 0) {
        channel_.assign(value_str ? value_str : "");
        channel_set_ = (value_str != nullptr);
        return ESP_OK;
    }

    if (strcmp(key, "loglevel") == 0) {
        // Accept numeric 0..5 mapping to esp_log_level_t
        int lvl = value_str ? atoi(value_str) : 2;
        if (lvl < (int)ESP_LOG_NONE) lvl = (int)ESP_LOG_NONE;
        if (lvl > (int)ESP_LOG_VERBOSE) lvl = (int)ESP_LOG_VERBOSE;
        loglevel_ = lvl;
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
    // Always include loglevel
    cJSON_AddNumberToObject(wifi_obj, "loglevel", loglevel_);
    added++;
    if (has_channel()) {
        cJSON_AddStringToObject(wifi_obj, "channel", channel_.c_str());
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


