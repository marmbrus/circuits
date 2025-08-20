#pragma once

#include "ConfigurationModule.h"
#include <string>
#include <vector>

// Forward declare to avoid adding heavy dependency to all includers
struct cJSON;

namespace config {

class MotionConfig : public ConfigurationModule {
public:
    MotionConfig();
    ~MotionConfig() override = default;

    // ConfigurationModule API
    const char* name() const override;
    const std::vector<ConfigurationValueDescriptor>& descriptors() const override;
    esp_err_t apply_update(const char* key, const char* value_str) override;
    esp_err_t to_json(struct cJSON* root_object) const override;

    // Accessors
    bool has_gpio() const { return gpio_set_; }
    int gpio() const { return gpio_; }

private:
    bool gpio_set_ = false;
    int gpio_ = -1;
    std::vector<ConfigurationValueDescriptor> descriptors_;
};

} // namespace config



