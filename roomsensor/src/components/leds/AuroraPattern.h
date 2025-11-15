#pragma once

#include "LEDPattern.h"
#include <cstddef>

namespace leds {

class AuroraPattern final : public LEDPattern {
public:
    const char* name() const override { return "AURORA"; }
    void reset(LEDStrip& strip, uint64_t now_us) override { start_us_ = now_us; }
    void update(LEDStrip& strip, uint64_t now_us) override;
    void set_speed_percent(int speed_percent) override { speed_percent_ = speed_percent; }
    void set_brightness_percent(int brightness_percent) override {
        if (brightness_percent < 0) brightness_percent = 0;
        if (brightness_percent > 100) brightness_percent = 100;
        brightness_percent_ = brightness_percent;
    }

private:
    uint64_t start_us_ = 0;
    int speed_percent_ = 50; // 0..100
    int brightness_percent_ = 100; // 0..100
    
    // Aurora color palette in RGB
    struct AuroraColor {
        uint8_t r, g, b;
    };
    
    static constexpr AuroraColor aurora_colors_[] = {
        {0, 255, 146},    // Bright green (most common aurora color)
        {0, 255, 100},    // Green
        {50, 255, 50},    // Yellow-green
        {0, 150, 255},    // Blue
        {100, 50, 255},   // Purple
        {150, 0, 255},    // Violet
        {255, 50, 150},   // Pink (rare but beautiful)
        {255, 100, 0},    // Orange-red (rare)
    };
    static constexpr size_t num_colors_ = sizeof(aurora_colors_) / sizeof(aurora_colors_[0]);
    
    // Generate smooth brightness variation using multiple sine waves
    float get_brightness_factor(float position, float time) const;
    
    // Interpolate between two aurora colors
    AuroraColor interpolate_color(const AuroraColor& c1, const AuroraColor& c2, float t) const;
};

} // namespace leds
