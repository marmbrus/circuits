#pragma once

#include "ConfigurationModule.h"
#include <string>

namespace config {

class LEDConfig : public ConfigurationModule {
public:
    explicit LEDConfig(const char* instance_name);

    const char* name() const override;
    const std::vector<ConfigurationValueDescriptor>& descriptors() const override;
    esp_err_t apply_update(const char* key, const char* value_str) override;
    esp_err_t to_json(struct cJSON* root_object) const override;

    // Available LED patterns for internal use
    enum class Pattern {
        INVALID = -1,
        OFF = 0,
        FADE,
        STATUS,
        SOLID,
        RAINBOW,
        LIFE,
        CHASE
    };

    // Supported LED chips for internal use
    enum class Chip {
        INVALID = -1,
        WS2812 = 0,
        SK6812,
        WS2814,
    };

    // Accessors
    bool has_data_gpio() const { return data_gpio_set_; }
    int data_gpio() const { return data_gpio_; }
    bool has_enabled_gpio() const { return enabled_gpio_set_; }
    int enabled_gpio() const { return enabled_gpio_; }
    const std::string& chip() const { return chip_; }
    Chip chip_enum() const { return chip_enum_; }
    int num_columns() const { return num_columns_; }
    int num_rows() const { return num_rows_; }

    bool has_pattern() const { return pattern_set_; }
    const std::string& pattern() const { return pattern_; }
    Pattern pattern_enum() const { return pattern_enum_; }
    bool has_r() const { return r_set_; }
    int r() const { return r_; }
    bool has_g() const { return g_set_; }
    int g() const { return g_; }
    bool has_b() const { return b_set_; }
    int b() const { return b_; }
    bool has_w() const { return w_set_; }
    int w() const { return w_; }
    bool has_brightness() const { return brightness_set_; }
    int brightness() const { return brightness_; }
    bool has_speed() const { return speed_set_; }
    int speed() const { return speed_; }
    bool has_start() const { return start_set_; }
    const std::string& start() const { return start_; }
    bool has_dma() const { return dma_set_; }
    bool dma() const { return dma_; }

private:
    // Parse from external string representation to internal enum. Returns INVALID on failure.
    static Pattern parse_pattern(const char* value);
    static const char* pattern_to_string(Pattern p);
    static Chip parse_chip(const char* value);
    static const char* chip_to_string(Chip c);

    std::string name_;

    // Persisted fields
    bool data_gpio_set_ = false;
    int data_gpio_ = -1;
    bool enabled_gpio_set_ = false;
    int enabled_gpio_ = -1;
    std::string chip_ = "WS2812"; // external/string representation
    Chip chip_enum_ = Chip::WS2812; // internal representation
    int num_columns_ = 1;
    int num_rows_ = 1;

    // Non-persisted runtime fields (loaded from NVS if present)
    bool pattern_set_ = false;
    std::string pattern_;
    Pattern pattern_enum_ = Pattern::OFF; // Internal representation
    bool r_set_ = false; int r_ = 0;
    bool g_set_ = false; int g_ = 0;
    bool b_set_ = false; int b_ = 0;
    bool w_set_ = false; int w_ = 0;
    bool brightness_set_ = false; int brightness_ = 100;
    bool speed_set_ = false; int speed_ = 100;
    bool start_set_ = false; std::string start_;
    bool dma_set_ = false; bool dma_ = false;

    std::vector<ConfigurationValueDescriptor> descriptors_;
};

} // namespace config


