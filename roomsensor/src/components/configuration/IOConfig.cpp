#include "IOConfig.h"
#include "cJSON.h"
#include <cstring>
#include <strings.h>

namespace config {

IOConfig::IOConfig(const char* instance_name) : name_(instance_name ? instance_name : "io1") {
    // Persisted descriptors: pin1config..pin8config
    for (int i = 1; i <= 8; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "pin%dconfig", i);
        descriptors_.push_back({strdup(key), ConfigValueType::String, nullptr, true});
    }
    // Persisted descriptors: pin1name..pin8name
    for (int i = 1; i <= 8; ++i) {
        char key[16];
        snprintf(key, sizeof(key), "pin%dname", i);
        descriptors_.push_back({strdup(key), ConfigValueType::String, nullptr, true});
    }
    // Non-persisted runtime values: switch1..switch8
    for (int i = 1; i <= 8; ++i) {
        char key[12];
        snprintf(key, sizeof(key), "switch%d", i);
        descriptors_.push_back({strdup(key), ConfigValueType::Bool, nullptr, false});
    }
}

const char* IOConfig::name() const { return name_.c_str(); }

const std::vector<ConfigurationValueDescriptor>& IOConfig::descriptors() const { return descriptors_; }

IOConfig::PinMode IOConfig::parse_pin_mode(const char* value) {
    if (!value) return PinMode::INVALID;
    if (strcmp(value, "SWITCH") == 0) return PinMode::SWITCH;
    if (strcmp(value, "SENSOR") == 0) return PinMode::SENSOR;
    return PinMode::INVALID;
}

const char* IOConfig::pin_mode_to_string(IOConfig::PinMode mode) {
    switch (mode) {
        case PinMode::SWITCH: return "SWITCH";
        case PinMode::SENSOR: return "SENSOR";
        case PinMode::INVALID: default: return "";
    }
}

esp_err_t IOConfig::apply_update(const char* key, const char* value_str) {
    if (!key) return ESP_ERR_INVALID_ARG;

    // pinNconfig
    if (strncmp(key, "pin", 3) == 0 && strstr(key, "config") != nullptr) {
        int idx = 0; // 1..8
        if (sscanf(key, "pin%dconfig", &idx) == 1 && idx >= 1 && idx <= 8) {
            PinMode mode = parse_pin_mode(value_str);
            if (mode == PinMode::INVALID) return ESP_ERR_INVALID_ARG;
            pin_modes_[idx - 1] = mode;
            pin_mode_set_[idx - 1] = true;
            return ESP_OK;
        }
        return ESP_ERR_INVALID_ARG;
    }

    // pinNname
    if (strncmp(key, "pin", 3) == 0 && strstr(key, "name") != nullptr) {
        int idx = 0; // 1..8
        if (sscanf(key, "pin%dname", &idx) == 1 && idx >= 1 && idx <= 8) {
            pin_name_set_[idx - 1] = (value_str != nullptr);
            pin_names_[idx - 1] = (value_str != nullptr) ? value_str : "";
            return ESP_OK;
        }
        return ESP_ERR_INVALID_ARG;
    }

    // switchN (non-persisted)
    if (strncmp(key, "switch", 6) == 0) {
        int idx = 0;
        if (sscanf(key, "switch%d", &idx) == 1 && idx >= 1 && idx <= 8) {
            // Interpret truthy/falsy
            bool v = false;
            if (value_str) {
                if (strcasecmp(value_str, "1") == 0 || strcasecmp(value_str, "true") == 0 ||
                    strcasecmp(value_str, "on") == 0 || strcasecmp(value_str, "yes") == 0) {
                    v = true;
                } else if (strcasecmp(value_str, "0") == 0 || strcasecmp(value_str, "false") == 0 ||
                           strcasecmp(value_str, "off") == 0 || strcasecmp(value_str, "no") == 0) {
                    v = false;
                } else {
                    return ESP_ERR_INVALID_ARG;
                }
            }
            switch_states_[idx - 1] = v;
            switch_state_set_[idx - 1] = true;
            return ESP_OK;
        }
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t IOConfig::to_json(cJSON* root_object) const {
    if (!root_object) return ESP_ERR_INVALID_ARG;

    // Include module only if at least one pin mode was explicitly set
    bool any = false;
    for (int i = 0; i < 8; ++i) { if (pin_mode_set_[i] || pin_name_set_[i] || switch_state_set_[i]) { any = true; break; } }
    if (!any) return ESP_OK;

    cJSON* obj = cJSON_CreateObject();
    for (int i = 0; i < 8; ++i) {
        if (pin_mode_set_[i]) {
            char key[16]; snprintf(key, sizeof(key), "pin%dconfig", i + 1);
            cJSON_AddStringToObject(obj, key, pin_mode_to_string(pin_modes_[i]));
        }
        if (pin_name_set_[i]) {
            char key[16]; snprintf(key, sizeof(key), "pin%dname", i + 1);
            cJSON_AddStringToObject(obj, key, pin_names_[i].c_str());
        }
        if (switch_state_set_[i]) {
            char key[12]; snprintf(key, sizeof(key), "switch%d", i + 1);
            cJSON_AddBoolToObject(obj, key, switch_states_[i]);
        }
    }

    cJSON_AddItemToObject(root_object, name(), obj);
    return ESP_OK;
}

IOConfig::PinMode IOConfig::pin_mode(int pin_index) const {
    if (pin_index < 1 || pin_index > 8) return PinMode::INVALID;
    return pin_modes_[pin_index - 1];
}

bool IOConfig::is_pin_mode_set(int pin_index) const {
    if (pin_index < 1 || pin_index > 8) return false;
    return pin_mode_set_[pin_index - 1];
}

bool IOConfig::switch_state(int pin_index) const {
    if (pin_index < 1 || pin_index > 8) return false;
    return switch_states_[pin_index - 1];
}

bool IOConfig::is_switch_state_set(int pin_index) const {
    if (pin_index < 1 || pin_index > 8) return false;
    return switch_state_set_[pin_index - 1];
}

const char* IOConfig::pin_name(int pin_index) const {
    if (pin_index < 1 || pin_index > 8) return "";
    return pin_names_[pin_index - 1].c_str();
}

bool IOConfig::is_pin_name_set(int pin_index) const {
    if (pin_index < 1 || pin_index > 8) return false;
    return pin_name_set_[pin_index - 1];
}

} // namespace config




