#include "GameOfLifeConfig.h"
#include "cJSON.h"
#include <cstring>

namespace config {

esp_err_t GameOfLifeConfig::apply_update(const char* key, const char* value_str) {
    if (!key) return ESP_ERR_INVALID_ARG;
    if (strcmp(key, "start") == 0) {
        if (value_str == nullptr || value_str[0] == '\0') {
            start_set_ = false;
            start_seed_.clear();
        } else {
            start_set_ = true;
            start_seed_ = value_str;
        }
        // generation bumped centrally by ConfigurationManager
        return ESP_OK;
    }
    if (strcmp(key, "restart") == 0) {
        // Tri-state: if unset, revert to default true
        if (value_str == nullptr || value_str[0] == '\0') {
            restart_set_ = false;
            restart_ = true;
            return ESP_OK;
        }
        if (strcasecmp(value_str, "1") == 0 || strcasecmp(value_str, "true") == 0 ||
            strcasecmp(value_str, "on") == 0 || strcasecmp(value_str, "yes") == 0) {
            restart_set_ = true;
            restart_ = true;
            return ESP_OK;
        }
        if (strcasecmp(value_str, "0") == 0 || strcasecmp(value_str, "false") == 0 ||
            strcasecmp(value_str, "off") == 0 || strcasecmp(value_str, "no") == 0) {
            restart_set_ = true;
            restart_ = false;
            return ESP_OK;
        }
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t GameOfLifeConfig::to_json(cJSON* root_object) const {
    if (!root_object) return ESP_ERR_INVALID_ARG;
    cJSON* obj = cJSON_CreateObject();
    if (start_set_) {
        cJSON_AddStringToObject(obj, "start", start_seed_.c_str());
    }
    if (restart_set_) {
        cJSON_AddBoolToObject(obj, "restart", restart_);
    }
    cJSON_AddItemToObject(root_object, name(), obj);
    return ESP_OK;
}

} // namespace config



