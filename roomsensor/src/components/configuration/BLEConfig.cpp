#include "BLEConfig.h"
#include "cJSON.h"
#include "esp_log.h"
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace config {

static const char* TAG = "BLEConfig";

BLEConfig::BLEConfig() {
    descriptors_.push_back({"scan", ConfigValueType::Bool, "0", true});
    descriptors_.push_back({"track", ConfigValueType::String, nullptr, true});
    descriptors_.push_back({"interval", ConfigValueType::I32, "10", true});
    descriptors_.push_back({"duty", ConfigValueType::I32, "50", true});
    descriptors_.push_back({"minRssi", ConfigValueType::I32, nullptr, true});
}

const char* BLEConfig::name() const {
    return "ble";
}

const std::vector<ConfigurationValueDescriptor>& BLEConfig::descriptors() const {
    return descriptors_;
}

static bool is_unset(const char* value_str) {
    return value_str == nullptr || value_str[0] == '\0';
}

// Parses a whole decimal integer within [lo, hi]
static bool parse_int(const char* value_str, int lo, int hi, int& out) {
    char* end = nullptr;
    long v = strtol(value_str, &end, 10);
    if (end == value_str || *end != '\0' || v < lo || v > hi) return false;
    out = (int)v;
    return true;
}

static bool parse_bool(const char* value_str, bool& out) {
    if (strcasecmp(value_str, "1") == 0 || strcasecmp(value_str, "true") == 0 ||
        strcasecmp(value_str, "on") == 0 || strcasecmp(value_str, "yes") == 0) {
        out = true;
        return true;
    }
    if (strcasecmp(value_str, "0") == 0 || strcasecmp(value_str, "false") == 0 ||
        strcasecmp(value_str, "off") == 0 || strcasecmp(value_str, "no") == 0) {
        out = false;
        return true;
    }
    return false;
}

esp_err_t BLEConfig::apply_update(const char* key, const char* value_str) {
    if (key == nullptr) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(mutex_);

    if (strcmp(key, "scan") == 0) {
        if (is_unset(value_str)) {
            settings_.scan = false;
            return ESP_OK;
        }
        return parse_bool(value_str, settings_.scan) ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    if (strcmp(key, "track") == 0) {
        std::string error;
        if (!ble::parse_track_list(value_str, settings_.tracked, &error)) {
            ESP_LOGW(TAG, "Rejected ble.track: %s", error.c_str());
            return ESP_ERR_INVALID_ARG;
        }
        return ESP_OK;
    }
    if (strcmp(key, "interval") == 0) {
        if (is_unset(value_str)) {
            settings_.interval_sec = kDefaultIntervalSec;
            return ESP_OK;
        }
        return parse_int(value_str, 1, 3600, settings_.interval_sec) ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    if (strcmp(key, "duty") == 0) {
        if (is_unset(value_str)) {
            settings_.duty_percent = kDefaultDutyPercent;
            return ESP_OK;
        }
        return parse_int(value_str, 1, 100, settings_.duty_percent) ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    if (strcmp(key, "minRssi") == 0) {
        if (is_unset(value_str)) {
            settings_.has_min_rssi = false;
            settings_.min_rssi = 0;
            return ESP_OK;
        }
        if (!parse_int(value_str, -127, 20, settings_.min_rssi)) return ESP_ERR_INVALID_ARG;
        settings_.has_min_rssi = true;
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t BLEConfig::to_json(cJSON* root_object) const {
    if (!root_object) return ESP_ERR_INVALID_ARG;
    std::lock_guard<std::mutex> lock(mutex_);

    // Only include the module once something is set, as motion and audio do
    const Settings& s = settings_;
    bool all_default = !s.scan && s.tracked.empty() && s.interval_sec == kDefaultIntervalSec &&
                       s.duty_percent == kDefaultDutyPercent && !s.has_min_rssi;
    if (all_default) return ESP_OK;

    cJSON* obj = cJSON_CreateObject();
    cJSON_AddBoolToObject(obj, "scan", s.scan);
    cJSON_AddStringToObject(obj, "track", ble::format_track_list(s.tracked).c_str());
    cJSON_AddNumberToObject(obj, "interval", s.interval_sec);
    cJSON_AddNumberToObject(obj, "duty", s.duty_percent);
    if (s.has_min_rssi) cJSON_AddNumberToObject(obj, "minRssi", s.min_rssi);
    cJSON_AddItemToObject(root_object, name(), obj);
    return ESP_OK;
}

BLEConfig::Settings BLEConfig::settings() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_;
}

bool BLEConfig::enabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return settings_.enabled();
}

} // namespace config
