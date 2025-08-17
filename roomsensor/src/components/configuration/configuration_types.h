#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

// Forward declaration to avoid pulling cJSON into all translation units
struct cJSON;

namespace config {

// Configuration value types, aligned with common NVS-capable primitives
enum class ConfigValueType : uint8_t {
    String = 0,
    I32,
    U32,
    I64,
    Bool,
    Blob,
    F32,
};

// Descriptor for a single configuration value
struct ConfigurationValueDescriptor {
    const char* name;                 // Key name (e.g., "ssid")
    ConfigValueType type;             // Value type
    const char* default_value;        // Optional default as string (nullptr if none)
    bool persisted;                   // True if stored in NVS
};

} // namespace config


