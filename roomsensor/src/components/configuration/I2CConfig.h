#pragma once

#include "ConfigurationModule.h"
#include "configuration_types.h"
#include <map>
#include <string>
#include <vector>

namespace config {

class I2CConfig final : public ConfigurationModule {
public:
    I2CConfig();
    ~I2CConfig() override = default;

    const char* name() const override { return "i2c"; }

    const std::vector<ConfigurationValueDescriptor>& descriptors() const override;

    esp_err_t apply_update(const char* key, const char* value_str) override;

    esp_err_t to_json(struct cJSON* root_object) const override;

    // Returns empty string if no explicit mapping exists
    std::string get_driver_for_address(uint8_t address_7bit) const;

    const std::map<std::string, std::string>& mappings() const { return address_to_driver_; }

private:
    static bool normalize_hex_key(const char* key_in, std::string& normalized);

    // Storage for configuration entries (normalized hex key -> driver name)
    std::map<std::string, std::string> address_to_driver_;

    // Descriptor cache and backing storage for key strings
    mutable std::vector<ConfigurationValueDescriptor> descriptors_cache_;
    mutable std::vector<std::string> descriptor_keys_;
};

} // namespace config


