#pragma once

#include "LEDPattern.h"
#include <cstddef>

namespace leds {

class JungleCanopyPattern final : public LEDPattern {
public:
    const char* name() const override { return "JUNGLE_CANOPY"; }
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
    
    // Jungle color palette
    struct JungleColor {
        uint8_t r, g, b;
    };
    
    static constexpr JungleColor jungle_colors_[] = {
        {20, 80, 20},     // Deep forest green
        {30, 100, 30},    // Dark green
        {40, 120, 40},    // Medium green
        {60, 140, 50},    // Bright green
        {80, 160, 60},    // Light green
        {100, 180, 70},   // Yellow-green
        {120, 200, 80},   // Bright yellow-green
        {150, 220, 90},   // Light yellow-green
        {180, 240, 120},  // Pale yellow-green
        {200, 255, 150},  // Bright yellow (sunlight)
        {220, 255, 180},  // Pale yellow (bright sunlight)
        {240, 255, 200},  // Very pale yellow-white (direct sun)
    };
    static constexpr size_t num_colors_ = sizeof(jungle_colors_) / sizeof(jungle_colors_[0]);
    
    // Generate layered canopy lighting with multiple effects
    float get_canopy_base_layer(float position, float time) const;
    float get_sunlight_patches(float position, float time) const;
    float get_leaf_movement(float position, float time) const;
    float get_wind_sway(float position, float time) const;
    
    // Color mixing and interpolation
    JungleColor interpolate_color(const JungleColor& c1, const JungleColor& c2, float t) const;
    JungleColor get_jungle_color(float position, float time, float brightness_factor) const;
    
    // Simple PRNG for organic randomness
    uint32_t simple_random(uint32_t seed) const;
    mutable uint32_t random_state_ = 54321;
};

} // namespace leds
