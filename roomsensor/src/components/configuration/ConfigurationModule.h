#pragma once

#include "esp_err.h"
#include "configuration_types.h"
#include <string>
#include <vector>

namespace config {

class ConfigurationModule {
public:
    virtual ~ConfigurationModule() = default;

    // Unique, short module name. Used for NVS namespace and MQTT topic segment
    virtual const char* name() const = 0;

    // Static descriptors of supported values (ownership remains with module)
    virtual const std::vector<ConfigurationValueDescriptor>& descriptors() const = 0;

    // Apply an update coming from NVS load, console, or MQTT
    // value is provided as a string; module performs parsing based on descriptor type
    virtual esp_err_t apply_update(const char* key, const char* value_str) = 0;

    // Serialize current module configuration into a provided cJSON object
    // The method should add an object under this module's name with key/value pairs
    virtual esp_err_t to_json(struct cJSON* root_object) const = 0;
};

} // namespace config


