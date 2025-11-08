#include "LEDConfig.h"
#include "cJSON.h"
#include <cstring>

namespace config {

LEDConfig::LEDConfig(const char* instance_name) : name_(instance_name ? instance_name : "led") {
    // Persisted descriptors
    descriptors_.push_back({"dataGPIO", ConfigValueType::I32, nullptr, true});
    descriptors_.push_back({"enabledGPIO", ConfigValueType::I32, nullptr, true});
    descriptors_.push_back({"enabledGPIOs", ConfigValueType::String, nullptr, true});
    descriptors_.push_back({"chip", ConfigValueType::String, "WS2812", true});
    descriptors_.push_back({"num_columns", ConfigValueType::I32, "1", true});
    descriptors_.push_back({"num_rows", ConfigValueType::I32, "1", true});
    descriptors_.push_back({"segment_rows", ConfigValueType::I32, nullptr, true});
    descriptors_.push_back({"layout", ConfigValueType::String, "ROW_MAJOR", true});
    descriptors_.push_back({"name", ConfigValueType::String, nullptr, true});

    // Non-persisted runtime values (still declared so they can be updated and optionally loaded once)
    // NOTE: The following keys are intentionally NOT persisted to avoid flash wear from frequent updates:
    //   - pattern, speed, brightness, R, G, B, W, dma
    // ConfigurationManager will still read any pre-provisioned string values from NVS (e.g., pattern)
    // regardless of the 'persisted' flag, allowing device-specific defaults without ongoing writes.
    descriptors_.push_back({"pattern", ConfigValueType::String, nullptr, false});
    descriptors_.push_back({"R", ConfigValueType::I32, nullptr, false});
    descriptors_.push_back({"G", ConfigValueType::I32, nullptr, false});
    descriptors_.push_back({"B", ConfigValueType::I32, nullptr, false});
    descriptors_.push_back({"W", ConfigValueType::I32, nullptr, false});
    descriptors_.push_back({"brightness", ConfigValueType::I32, nullptr, false});
    descriptors_.push_back({"speed", ConfigValueType::I32, nullptr, false});
    descriptors_.push_back({"dma", ConfigValueType::Bool, nullptr, false});
}

const char* LEDConfig::name() const {
    return name_.c_str();
}

const std::vector<ConfigurationValueDescriptor>& LEDConfig::descriptors() const {
    return descriptors_;
}

LEDConfig::Chip LEDConfig::parse_chip(const char* value) {
    if (!value) return Chip::INVALID;
    if (strcmp(value, "WS2812") == 0) return Chip::WS2812;
    if (strcmp(value, "SK6812") == 0) return Chip::SK6812;
    if (strcmp(value, "WS2814") == 0) return Chip::WS2814;
    if (strcmp(value, "FLIPDOT") == 0) return Chip::FLIPDOT;
    return Chip::INVALID;
}

const char* LEDConfig::chip_to_string(LEDConfig::Chip c) {
    switch (c) {
        case Chip::WS2812: return "WS2812";
        case Chip::SK6812: return "SK6812";
        case Chip::WS2814: return "WS2814";
        case Chip::FLIPDOT: return "FLIPDOT";
        case Chip::INVALID: default: return "WS2812";
    }
}

LEDConfig::Layout LEDConfig::parse_layout(const char* value) {
    if (!value) return Layout::ROW_MAJOR;
    if (strcmp(value, "ROW_MAJOR") == 0) return Layout::ROW_MAJOR;
    if (strcmp(value, "SERPENTINE_ROW") == 0) return Layout::SERPENTINE_ROW;
    if (strcmp(value, "SERPENTINE_COLUMN") == 0) return Layout::SERPENTINE_COLUMN;
    if (strcmp(value, "COLUMN_MAJOR") == 0) return Layout::COLUMN_MAJOR;
    if (strcmp(value, "FLIPDOT_GRID") == 0) return Layout::FLIPDOT_GRID;
    return Layout::ROW_MAJOR;
}

const char* LEDConfig::layout_to_string(LEDConfig::Layout l) {
    switch (l) {
        case Layout::ROW_MAJOR: return "ROW_MAJOR";
        case Layout::SERPENTINE_ROW: return "SERPENTINE_ROW";
        case Layout::SERPENTINE_COLUMN: return "SERPENTINE_COLUMN";
        case Layout::COLUMN_MAJOR: return "COLUMN_MAJOR";
        case Layout::FLIPDOT_GRID: return "FLIPDOT_GRID";
    }
    return "ROW_MAJOR";
}

LEDConfig::Pattern LEDConfig::parse_pattern(const char* value) {
    if (!value) return Pattern::INVALID;
    if (strcmp(value, "OFF") == 0) return Pattern::OFF;
    if (strcmp(value, "FADE") == 0) return Pattern::FADE;
    if (strcmp(value, "STATUS") == 0) return Pattern::STATUS;
    if (strcmp(value, "SOLID") == 0) return Pattern::SOLID;
    if (strcmp(value, "RAINBOW") == 0) return Pattern::RAINBOW;
    if (strcmp(value, "LIFE") == 0) return Pattern::LIFE;
    if (strcmp(value, "CHASE") == 0) return Pattern::CHASE;
    if (strcmp(value, "POSITION") == 0) return Pattern::POSITION;
    if (strcmp(value, "CLOCK") == 0) return Pattern::CLOCK;
    if (strcmp(value, "CALENDAR") == 0) return Pattern::CALENDAR;
    return Pattern::INVALID;
}

const char* LEDConfig::pattern_to_string(LEDConfig::Pattern p) {
    switch (p) {
        case Pattern::INVALID: return "OFF"; // treat as OFF when stringifying
        case Pattern::OFF: return "OFF";
        case Pattern::FADE: return "FADE";
        case Pattern::STATUS: return "STATUS";
        case Pattern::SOLID: return "SOLID";
        case Pattern::RAINBOW: return "RAINBOW";
        case Pattern::LIFE: return "LIFE";
        case Pattern::CHASE: return "CHASE";
        case Pattern::POSITION: return "POSITION";
        case Pattern::CLOCK: return "CLOCK";
        case Pattern::CALENDAR: return "CALENDAR";
    }
    return "OFF";
}

esp_err_t LEDConfig::apply_update(const char* key, const char* value_str) {
    if (key == nullptr) return ESP_ERR_INVALID_ARG;

    if (strcmp(key, "dataGPIO") == 0) {
        data_gpio_set_ = (value_str != nullptr);
        data_gpio_ = value_str ? atoi(value_str) : -1;
        /* generation bumped centrally */
        return ESP_OK;
    }
    if (strcmp(key, "enabledGPIO") == 0) {
        enabled_gpio_set_ = (value_str != nullptr);
        enabled_gpio_ = value_str ? atoi(value_str) : -1;
        /* generation bumped centrally */
        return ESP_OK;
    }
    if (strcmp(key, "enabledGPIOs") == 0) {
        enabled_gpios_.clear();
        if (value_str == nullptr || *value_str == '\0') {
            enabled_gpios_set_ = false;
            /* generation bumped centrally */
            return ESP_OK;
        }
        // Parse comma-separated integers; ignore invalid tokens
        const char* p = value_str;
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ',') p++; // skip delimiters
            if (!*p) break;
            // collect token
            const char* start = p;
            while (*p && *p != ',') p++;
            std::string tok(start, p - start);
            // trim whitespace
            size_t a = tok.find_first_not_of(" \t\n\r");
            size_t b = tok.find_last_not_of(" \t\n\r");
            if (a != std::string::npos && b != std::string::npos) {
                tok = tok.substr(a, b - a + 1);
                if (!tok.empty()) {
                    int v = atoi(tok.c_str());
                    if (v >= 0) enabled_gpios_.push_back(v);
                }
            }
        }
        // dedupe
        std::sort(enabled_gpios_.begin(), enabled_gpios_.end());
        enabled_gpios_.erase(std::unique(enabled_gpios_.begin(), enabled_gpios_.end()), enabled_gpios_.end());
        enabled_gpios_set_ = !enabled_gpios_.empty();
        /* generation bumped centrally */
        return ESP_OK;
    }
    if (strcmp(key, "chip") == 0) {
        Chip parsed = parse_chip(value_str);
        if (parsed != Chip::INVALID) {
            chip_enum_ = parsed;
            chip_ = chip_to_string(parsed);
            /* generation bumped centrally */
            return ESP_OK;
        }
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(key, "num_columns") == 0) {
        num_columns_ = value_str ? atoi(value_str) : 1;
        if (num_columns_ <= 0) num_columns_ = 1;
        /* generation bumped centrally */
        return ESP_OK;
    }
    if (strcmp(key, "num_rows") == 0) {
        num_rows_ = value_str ? atoi(value_str) : 1;
        if (num_rows_ <= 0) num_rows_ = 1;
        /* generation bumped centrally */
        return ESP_OK;
    }
    if (strcmp(key, "segment_rows") == 0) {
        segment_rows_set_ = (value_str != nullptr);
        segment_rows_ = value_str ? atoi(value_str) : 0;
        if (segment_rows_ < 0) segment_rows_ = 0; // 0 => whole height
        /* generation bumped centrally */
        return ESP_OK;
    }
    if (strcmp(key, "layout") == 0) {
        Layout parsed = parse_layout(value_str);
        layout_enum_ = parsed;
        layout_ = layout_to_string(parsed);
        /* generation bumped centrally */
        return ESP_OK;
    }
    if (strcmp(key, "name") == 0) {
        if (value_str == nullptr || *value_str == '\0') {
            display_name_.clear();
            name_set_ = false;
            /* generation bumped centrally */
            return ESP_OK;
        }
        display_name_ = value_str;
        name_set_ = true;
        /* generation bumped centrally */
        return ESP_OK;
    }

    // Non-persisted
    if (strcmp(key, "pattern") == 0) {
        Pattern parsed = parse_pattern(value_str);
        if (parsed != Pattern::INVALID) {
            pattern_enum_ = parsed;
            pattern_ = pattern_to_string(parsed);
            pattern_set_ = true;
            bump_generation();
            return ESP_OK;
        }
        return ESP_ERR_INVALID_ARG;
    }
    if (strcmp(key, "R") == 0) { r_ = value_str ? atoi(value_str) : 0; r_set_ = (value_str != nullptr); bump_generation(); return ESP_OK; }
    if (strcmp(key, "G") == 0) { g_ = value_str ? atoi(value_str) : 0; g_set_ = (value_str != nullptr); bump_generation(); return ESP_OK; }
    if (strcmp(key, "B") == 0) { b_ = value_str ? atoi(value_str) : 0; b_set_ = (value_str != nullptr); bump_generation(); return ESP_OK; }
    if (strcmp(key, "W") == 0) { w_ = value_str ? atoi(value_str) : 0; w_set_ = (value_str != nullptr); bump_generation(); return ESP_OK; }
    if (strcmp(key, "brightness") == 0) {
        brightness_ = value_str ? atoi(value_str) : 100;
        if (brightness_ < 0) brightness_ = 0;
        if (brightness_ > 100) brightness_ = 100;
        brightness_set_ = (value_str != nullptr);
        /* generation bumped centrally */
        return ESP_OK;
    }
    if (strcmp(key, "speed") == 0) {
        speed_ = value_str ? atoi(value_str) : 100;
        if (speed_ < 0) speed_ = 0;
        if (speed_ > 100) speed_ = 100;
        speed_set_ = (value_str != nullptr);
        /* generation bumped centrally */
        return ESP_OK;
    }
    // removed legacy 'start' from LEDConfig; now in life module
    if (strcmp(key, "dma") == 0) {
        // Tri-state: if value_str is null, clear; otherwise parse truthy/falsy
        if (value_str == nullptr || value_str[0] == '\0') {
            dma_set_ = false; // unset -> auto-assign
            dma_ = false;
            bump_generation();
            return ESP_OK;
        }
        // Accept common boolean strings
        if (strcasecmp(value_str, "1") == 0 || strcasecmp(value_str, "true") == 0 || strcasecmp(value_str, "on") == 0 || strcasecmp(value_str, "yes") == 0) {
            dma_set_ = true;
            dma_ = true;
            bump_generation();
            return ESP_OK;
        }
        if (strcasecmp(value_str, "0") == 0 || strcasecmp(value_str, "false") == 0 || strcasecmp(value_str, "off") == 0 || strcasecmp(value_str, "no") == 0) {
            dma_set_ = true;
            dma_ = false;
            bump_generation();
            return ESP_OK;
        }
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t LEDConfig::to_json(cJSON* root_object) const {
    if (!root_object) return ESP_ERR_INVALID_ARG;
    cJSON* obj = cJSON_CreateObject();

    // Persisted fields
    if (data_gpio_set_) cJSON_AddNumberToObject(obj, "dataGPIO", data_gpio_);
    // Only include one representation: prefer plural if set; otherwise include singular if non-negative
    if (enabled_gpios_set_ && !enabled_gpios_.empty()) {
        // Join into comma-separated string
        std::string s;
        for (size_t i = 0; i < enabled_gpios_.size(); ++i) {
            if (i) s += ",";
            s += std::to_string(enabled_gpios_[i]);
        }
        cJSON_AddStringToObject(obj, "enabledGPIOs", s.c_str());
    } else if (enabled_gpio_set_ && enabled_gpio_ >= 0) {
        cJSON_AddNumberToObject(obj, "enabledGPIO", enabled_gpio_);
    }
    cJSON_AddStringToObject(obj, "chip", chip_.c_str());
    cJSON_AddNumberToObject(obj, "num_columns", num_columns_);
    cJSON_AddNumberToObject(obj, "num_rows", num_rows_);
    if (segment_rows_set_) cJSON_AddNumberToObject(obj, "segment_rows", segment_rows_);
    cJSON_AddStringToObject(obj, "layout", layout_.c_str());
    if (name_set_) cJSON_AddStringToObject(obj, "name", display_name_.c_str());

    // Non-persisted runtime fields (include only if set)
    if (pattern_set_) cJSON_AddStringToObject(obj, "pattern", pattern_.c_str());
    if (r_set_) cJSON_AddNumberToObject(obj, "R", r_);
    if (g_set_) cJSON_AddNumberToObject(obj, "G", g_);
    if (b_set_) cJSON_AddNumberToObject(obj, "B", b_);
    if (w_set_) cJSON_AddNumberToObject(obj, "W", w_);
    if (brightness_set_) cJSON_AddNumberToObject(obj, "brightness", brightness_);
    if (speed_set_) cJSON_AddNumberToObject(obj, "speed", speed_);
    // 'start' now belongs to life module
    if (dma_set_) cJSON_AddBoolToObject(obj, "dma", dma_);

    // If dataGPIO is not set, omit this module from the config entirely
    if (!data_gpio_set_) {
        cJSON_Delete(obj);
        return ESP_OK;
    }

    cJSON_AddItemToObject(root_object, name(), obj);
    return ESP_OK;
}

} // namespace config


