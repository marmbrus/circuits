#include "I2CConfig.h"
#include "cJSON.h"
#include "esp_log.h"
#include <algorithm>
#include <cstring>
#include <stdio.h>

namespace config {

static const char* TAG = "I2CConfig";

// Construct descriptors for valid 7-bit I2C addresses commonly used: 0x08..0x77
I2CConfig::I2CConfig() {}

static std::string to_hex_key(uint8_t addr) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", addr);
    return std::string(buf);
}

const std::vector<ConfigurationValueDescriptor>& I2CConfig::descriptors() const {
    if (descriptors_cache_.empty()) {
        descriptor_keys_.reserve(0x78 - 0x08);
        descriptors_cache_.reserve(0x78 - 0x08);
        for (uint8_t a = 0x08; a < 0x78; ++a) {
            descriptor_keys_.push_back(to_hex_key(a));
            const std::string& key = descriptor_keys_.back();
            ConfigurationValueDescriptor d{ key.c_str(), ConfigValueType::String, nullptr, true };
            descriptors_cache_.push_back(d);
        }
    }
    return descriptors_cache_;
}

bool I2CConfig::normalize_hex_key(const char* key_in, std::string& normalized) {
    if (key_in == nullptr || *key_in == '\0') return false;
    // Accept forms: "2a", "0x2a", "0X2A"; produce lowercase without prefix
    const char* p = key_in;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    // Expect 1-2 hex digits; pad to two characters
    size_t len = strlen(p);
    if (len == 0 || len > 2) return false;
    auto hex_digit = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };
    int value = 0;
    for (size_t i = 0; i < len; ++i) {
        int v = hex_digit(p[i]);
        if (v < 0) return false;
        value = (value << 4) | v;
    }
    if (value < 0x08 || value > 0x77) return false;
    char out[3];
    snprintf(out, sizeof(out), "%02x", value);
    normalized.assign(out);
    return true;
}

esp_err_t I2CConfig::apply_update(const char* key, const char* value_str) {
    std::string norm;
    if (!normalize_hex_key(key, norm)) {
        ESP_LOGW(TAG, "Invalid I2C address key: %s", key ? key : "(null)");
        return ESP_ERR_INVALID_ARG;
    }

    if (value_str == nullptr || value_str[0] == '\0') {
        // Unset mapping
        auto it = address_to_driver_.find(norm);
        if (it != address_to_driver_.end()) {
            address_to_driver_.erase(it);
            bump_generation();
        }
        return ESP_OK;
    }

    // Store provided driver name as-is
    address_to_driver_[norm] = std::string(value_str);
    bump_generation();
    return ESP_OK;
}

esp_err_t I2CConfig::to_json(struct cJSON* root_object) const {
    if (!root_object) return ESP_ERR_INVALID_ARG;
    cJSON* obj = cJSON_CreateObject();
    for (const auto& kv : address_to_driver_) {
        cJSON_AddStringToObject(obj, kv.first.c_str(), kv.second.c_str());
    }
    cJSON_AddItemToObject(root_object, name(), obj);
    return ESP_OK;
}

std::string I2CConfig::get_driver_for_address(uint8_t address_7bit) const {
    if (address_7bit < 0x08 || address_7bit > 0x77) return std::string();
    std::string key = to_hex_key(address_7bit);
    auto it = address_to_driver_.find(key);
    if (it == address_to_driver_.end()) return std::string();
    return it->second;
}

} // namespace config


