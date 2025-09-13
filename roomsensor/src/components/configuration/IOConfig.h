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
        SWITCH,       // Legacy: ON drives LOW (active-low)
        SWITCH_HIGH,  // ON drives HIGH (active-high)
        SWITCH_LOW,   // ON drives LOW (active-low)
        SENSOR,   // Input pin, reported via metric when state changes
    };

    enum class Logic : uint8_t {
        NONE = 0,
        LOCK_KEYPAD,
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
    // Base (externally-set) switch state
    bool base_switch_state(int pin_index /*1..8*/) const;
    bool is_base_switch_state_set(int pin_index /*1..8*/) const;

    // Optional per-module logic selection
    Logic logic() const { return logic_; }
    bool is_logic_set() const { return logic_set_; }

    // Optional human-friendly pin name
    const char* pin_name(int pin_index /*1..8*/) const;
    bool is_pin_name_set(int pin_index /*1..8*/) const;

    // Runtime contact state for SENSOR pins (non-persisted)
    void set_contact_state(int pin_index /*1..8*/, bool closed);
    bool contact_state(int pin_index /*1..8*/) const;
    bool is_contact_state_set(int pin_index /*1..8*/) const;

    // Runtime control of SWITCH outputs (non-persisted)
    void set_switch_state(int pin_index /*1..8*/, bool on);
    void clear_switch_state(int pin_index /*1..8*/);
    void reset_effective_switches_to_base();
    void get_effective_switch_snapshot(uint8_t &set_mask_out, uint8_t &on_mask_out) const;

    static PinMode parse_pin_mode(const char* value);
    static const char* pin_mode_to_string(PinMode mode);
    static Logic parse_logic(const char* value);
    static const char* logic_to_string(Logic lgc);

private:
    std::string name_;
    std::vector<ConfigurationValueDescriptor> descriptors_;

    PinMode pin_modes_[8] = {PinMode::INVALID, PinMode::INVALID, PinMode::INVALID, PinMode::INVALID,
                              PinMode::INVALID, PinMode::INVALID, PinMode::INVALID, PinMode::INVALID};
    bool pin_mode_set_[8] = {false, false, false, false, false, false, false, false};

    bool switch_states_[8] = {false, false, false, false, false, false, false, false};
    bool switch_state_set_[8] = {false, false, false, false, false, false, false, false};
    bool base_switch_states_[8] = {false, false, false, false, false, false, false, false};
    bool base_switch_state_set_[8] = {false, false, false, false, false, false, false, false};

    // Persisted pin names (pin1name..pin8name)
    std::string pin_names_[8] = {"", "", "", "", "", "", "", ""};
    bool pin_name_set_[8] = {false, false, false, false, false, false, false, false};

    // Non-persisted contact states for SENSOR pins
    bool contact_states_[8] = {false, false, false, false, false, false, false, false};
    bool contact_state_set_[8] = {false, false, false, false, false, false, false, false};

    // Optional logic selection (persisted)
    Logic logic_ = Logic::NONE;
    bool logic_set_ = false;
};

} // namespace config




