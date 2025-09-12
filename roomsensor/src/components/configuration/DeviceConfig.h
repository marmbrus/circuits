#pragma once

#include "ConfigurationModule.h"
#include <string>
#include <vector>

namespace config {

class DeviceConfig : public ConfigurationModule {
public:
    DeviceConfig();
    ~DeviceConfig() override;

    const char* name() const override { return "device"; }
    const std::vector<ConfigurationValueDescriptor>& descriptors() const override;

    esp_err_t apply_update(const char* key, const char* value_str) override;
    esp_err_t to_json(cJSON* root_object) const override;

    const std::string& type() const { return type_; }

private:
    std::string type_;
    static const std::vector<ConfigurationValueDescriptor> descriptors_;
};

} // namespace config
