#pragma once

#include "ConfigurationModule.h"
#include <string>
#include <vector>
#include <algorithm>

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
        CHASE,
        POSITION,
        CLOCK,
        CALENDAR,
        SUMMARY,
        SWEEP,
        METEOR,
        SUNSET
    };

    // Supported LED chips for internal use
    enum class Chip {
        INVALID = -1,
        WS2812 = 0,
        SK6812,
        WS2814,
        FLIPDOT,
    };

    // Grid layout for mapping logical (row,col) to physical. Defaults to ROW_MAJOR.
    enum class Layout {
        ROW_MAJOR = 0,
        SERPENTINE_ROW,
        SERPENTINE_COLUMN,
        COLUMN_MAJOR,
        FLIPDOT_GRID,
    };

    // Accessors
    bool has_data_gpio() const { return data_gpio_set_; }
    int data_gpio() const { return data_gpio_; }
    bool has_enabled_gpio() const { return enabled_gpio_set_; }
    int enabled_gpio() const { return enabled_gpio_; }
    // New plural enables
    bool has_enabled_gpios() const { return enabled_gpios_set_ && !enabled_gpios_.empty(); }
    const std::vector<int>& enabled_gpios() const { return enabled_gpios_; }
    // Convenience: union of plural (if set) and singular (if set)
    std::vector<int> all_enabled_gpios() const {
        std::vector<int> pins;
        // If plural is set, prefer it exclusively and ignore singular
        if (has_enabled_gpios()) {
            pins.insert(pins.end(), enabled_gpios_.begin(), enabled_gpios_.end());
        } else if (has_enabled_gpio() && enabled_gpio_ >= 0) {
            pins.push_back(enabled_gpio_);
        }
        // de-dup
        std::sort(pins.begin(), pins.end());
        pins.erase(std::unique(pins.begin(), pins.end()), pins.end());
        return pins;
    }
    const std::string& chip() const { return chip_; }
    Chip chip_enum() const { return chip_enum_; }
    int num_columns() const { return num_columns_; }
    int num_rows() const { return num_rows_; }
    bool has_segment_rows() const { return segment_rows_set_; }
    int segment_rows() const { return segment_rows_; }
    const std::string& layout() const { return layout_; }
    Layout layout_enum() const { return layout_enum_; }

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
    bool has_dma() const { return dma_set_; }
    bool dma() const { return dma_; }
    // Optional user-provided display name
    bool has_display_name() const { return name_set_; }
    const std::string& display_name() const { return display_name_; }

private:
    // Parse from external string representation to internal enum. Returns INVALID on failure.
    static Pattern parse_pattern(const char* value);
    static const char* pattern_to_string(Pattern p);
    static Chip parse_chip(const char* value);
    static const char* chip_to_string(Chip c);
    static Layout parse_layout(const char* value);
    static const char* layout_to_string(Layout l);

    std::string name_;

    // Persisted fields
    bool data_gpio_set_ = false;
    int data_gpio_ = -1;
    bool enabled_gpio_set_ = false;
    int enabled_gpio_ = -1;
    bool enabled_gpios_set_ = false;
    std::vector<int> enabled_gpios_;
    std::string chip_ = "WS2812"; // external/string representation
    Chip chip_enum_ = Chip::WS2812; // internal representation
    int num_columns_ = 1;
    int num_rows_ = 1;
    bool segment_rows_set_ = false; int segment_rows_ = 0; // 0 or unset => whole height
    std::string layout_ = "ROW_MAJOR";
    Layout layout_enum_ = Layout::ROW_MAJOR;
    // Optional persisted display name
    bool name_set_ = false; std::string display_name_;

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
    bool dma_set_ = false; bool dma_ = false;

    std::vector<ConfigurationValueDescriptor> descriptors_;
};

} // namespace config


