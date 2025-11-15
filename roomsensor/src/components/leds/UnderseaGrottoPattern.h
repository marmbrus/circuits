#pragma once

#include "LEDPattern.h"
#include <cstddef>

namespace leds {

class UnderseaGrottoPattern final : public LEDPattern {
public:
    const char* name() const override { return "UNDERSEA_GROTTO"; }
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
    
    // Oceanic color palette - from deep ocean to surface light
    struct OceanColor {
        uint8_t r, g, b;
    };
    
    static constexpr OceanColor ocean_colors_[] = {
        {5, 15, 40},      // Deep ocean trench (very dark blue)
        {8, 25, 60},      // Deep water
        {12, 35, 80},     // Mid-deep ocean
        {18, 45, 100},    // Deep blue
        {25, 60, 120},    // Ocean blue
        {35, 75, 140},    // Medium blue
        {45, 90, 160},    // Lighter blue
        {60, 110, 180},   // Blue-cyan
        {75, 130, 200},   // Light blue
        {90, 150, 220},   // Bright blue
        {110, 170, 240},  // Very light blue
        {130, 190, 255},  // Pale blue (surface light)
        {150, 210, 255},  // Bright surface light
        {180, 230, 255},  // Very bright surface (rare)
    };
    static constexpr size_t num_colors_ = sizeof(ocean_colors_) / sizeof(ocean_colors_[0]);
    
    // Generate layered underwater lighting effects
    float get_water_depth_layer(float position, float time) const;
    float get_wave_motion(float position, float time) const;
    float get_surface_light_rays(float position, float time) const;
    float get_water_turbulence(float position, float time) const;
    float get_gentle_current(float position, float time) const;
    
    // Color mixing and interpolation
    OceanColor interpolate_color(const OceanColor& c1, const OceanColor& c2, float t) const;
    OceanColor get_ocean_color(float position, float time, float depth_brightness) const;
    
    // Simple PRNG for organic water effects
    uint32_t simple_random(uint32_t seed) const;
    mutable uint32_t random_state_ = 98765;
};

} // namespace leds
