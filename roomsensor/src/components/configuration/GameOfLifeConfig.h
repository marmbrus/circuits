#pragma once

#include "ConfigurationModule.h"
#include <string>

namespace config {

class GameOfLifeConfig : public ConfigurationModule {
public:
    GameOfLifeConfig() = default;
    ~GameOfLifeConfig() override = default;

    // Namespace name
    const char* name() const override { return "life"; }

    // Descriptors: game-of-life specific controls
    const std::vector<ConfigurationValueDescriptor>& descriptors() const override { return descriptors_; }

    // Updates come as strings; parse and store
    esp_err_t apply_update(const char* key, const char* value_str) override;

    // Serialize to JSON
    esp_err_t to_json(struct cJSON* root_object) const override;

    // Accessors
    bool has_start() const { return start_set_; }
    const std::string& start() const { return start_seed_; }
    // Restart behavior (non-persisted, defaults to true)
    bool restart_enabled() const { return restart_; }
    bool has_restart() const { return restart_set_; }

private:
    bool start_set_ = false;
    std::string start_seed_;
    bool restart_set_ = false;
    bool restart_ = true; // default to true unless explicitly set
    std::vector<ConfigurationValueDescriptor> descriptors_{
        // Keep non-persisted to avoid flash wear on frequent tuning; still load if pre-provisioned
        {"start", ConfigValueType::String, nullptr, false},
        {"restart", ConfigValueType::Bool, "true", false},
    };
};

} // namespace config



