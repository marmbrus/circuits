#pragma once

#include "ConfigurationModule.h"
#include "configuration_types.h"
#include <string>
#include <vector>

// Forward declare to avoid heavy include in header
struct cJSON;

namespace config {

class IOConfig : public ConfigurationModule {
public:
    enum class PinMode : uint8_t {
        INVALID = 0,
        SWITCH,   // Output pin controlled by non-persisted switchN
        SENSOR,   // Input pin, reported via metric when state changes
    };

    explicit IOConfig(const char* instance_name);
    ~IOConfig() override = default;

    // ConfigurationModule API
    const char* name() const override;
    const std::vector<ConfigurationValueDescriptor>& descriptors() const override;
    esp_err_t apply_update(const char* key, const char* value_str) override;
    esp_err_t to_json(struct cJSON* root_object) const override;

    // Accessors
    PinMode pin_mode(int pin_index /*1..8*/) const;
    bool is_pin_mode_set(int pin_index /*1..8*/) const;
    bool switch_state(int pin_index /*1..8*/) const;
    bool is_switch_state_set(int pin_index /*1..8*/) const;

    // Optional human-friendly pin name
    const char* pin_name(int pin_index /*1..8*/) const;
    bool is_pin_name_set(int pin_index /*1..8*/) const;

    static PinMode parse_pin_mode(const char* value);
    static const char* pin_mode_to_string(PinMode mode);

private:
    std::string name_;
    std::vector<ConfigurationValueDescriptor> descriptors_;

    PinMode pin_modes_[8] = {PinMode::INVALID, PinMode::INVALID, PinMode::INVALID, PinMode::INVALID,
                              PinMode::INVALID, PinMode::INVALID, PinMode::INVALID, PinMode::INVALID};
    bool pin_mode_set_[8] = {false, false, false, false, false, false, false, false};

    bool switch_states_[8] = {false, false, false, false, false, false, false, false};
    bool switch_state_set_[8] = {false, false, false, false, false, false, false, false};

    // Persisted pin names (pin1name..pin8name)
    std::string pin_names_[8] = {"", "", "", "", "", "", "", ""};
    bool pin_name_set_[8] = {false, false, false, false, false, false, false, false};
};

} // namespace config




