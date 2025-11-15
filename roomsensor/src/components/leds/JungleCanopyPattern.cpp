#include "JungleCanopyPattern.h"
#include "LEDStrip.h"
#include <math.h>
#include <algorithm>

namespace leds {

constexpr JungleCanopyPattern::JungleColor JungleCanopyPattern::jungle_colors_[];

uint32_t JungleCanopyPattern::simple_random(uint32_t seed) const {
    // Simple linear congruential generator for organic variation
    return seed * 1103515245 + 12345;
}

float JungleCanopyPattern::get_canopy_base_layer(float position, float time) const {
    // Create the base canopy density using multiple overlapping sine waves
    // This represents the overall leaf coverage and density variations
    
    float wave1 = sinf(position * 0.3f + time * 0.1f) * 0.3f;
    float wave2 = sinf(position * 0.7f + time * 0.15f) * 0.2f;
    float wave3 = sinf(position * 1.1f + time * 0.08f) * 0.15f;
    float wave4 = sinf(position * 1.8f + time * 0.12f) * 0.1f;
    
    // Combine waves to create organic canopy density
    float combined = wave1 + wave2 + wave3 + wave4;
    
    // Map to 0.2 - 0.8 range (never completely dark, never completely bright)
    return 0.5f + combined;
}

float JungleCanopyPattern::get_sunlight_patches(float position, float time) const {
    // Create moving patches of sunlight that break through the canopy
    // These should be less frequent but more dramatic
    
    // Large, slow-moving sunlight patches
    float sun_patch1 = sinf(position * 0.4f - time * 0.05f) * 0.4f;
    float sun_patch2 = sinf(position * 0.6f + time * 0.07f) * 0.3f;
    
    // Smaller, more frequent light spots
    float light_spot1 = sinf(position * 1.5f - time * 0.2f) * 0.2f;
    float light_spot2 = sinf(position * 2.1f + time * 0.18f) * 0.15f;
    
    // Combine and apply threshold to create distinct patches
    float combined = sun_patch1 + sun_patch2 + light_spot1 + light_spot2;
    
    // Use a power function to create more distinct bright patches
    float normalized = (combined + 1.0f) * 0.5f; // Map to 0-1
    return powf(normalized, 2.5f); // Make bright areas brighter, dim areas dimmer
}

float JungleCanopyPattern::get_leaf_movement(float position, float time) const {
    // Simulate individual leaves moving and creating small light variations
    // High frequency, low amplitude variations
    
    float leaf1 = sinf(position * 3.2f + time * 0.8f) * 0.08f;
    float leaf2 = sinf(position * 4.7f - time * 0.6f) * 0.06f;
    float leaf3 = sinf(position * 6.1f + time * 0.9f) * 0.04f;
    float leaf4 = sinf(position * 7.8f - time * 0.7f) * 0.03f;
    
    return leaf1 + leaf2 + leaf3 + leaf4;
}

float JungleCanopyPattern::get_wind_sway(float position, float time) const {
    // Large-scale swaying motion of the entire canopy
    // Low frequency, medium amplitude
    
    float sway1 = sinf(position * 0.2f + time * 0.3f) * 0.15f;
    float sway2 = sinf(position * 0.35f - time * 0.25f) * 0.1f;
    
    return sway1 + sway2;
}

JungleCanopyPattern::JungleColor JungleCanopyPattern::interpolate_color(const JungleColor& c1, const JungleColor& c2, float t) const {
    if (t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;
    
    return {
        static_cast<uint8_t>(c1.r + (c2.r - c1.r) * t),
        static_cast<uint8_t>(c1.g + (c2.g - c1.g) * t),
        static_cast<uint8_t>(c1.b + (c2.b - c1.b) * t)
    };
}

JungleCanopyPattern::JungleColor JungleCanopyPattern::get_jungle_color(float position, float time, float brightness_factor) const {
    // Select base color based on brightness - darker areas are more green, brighter areas more yellow
    float color_position = brightness_factor; // Use brightness to drive color selection
    
    // Map brightness to color palette position
    float color_index_f = color_position * static_cast<float>(num_colors_ - 1);
    size_t color_index = static_cast<size_t>(floorf(color_index_f));
    color_index = std::min(color_index, num_colors_ - 1);
    size_t next_color_index = std::min(color_index + 1, num_colors_ - 1);
    float color_blend = color_index_f - floorf(color_index_f);
    
    // Interpolate between colors
    JungleColor base_color = interpolate_color(
        jungle_colors_[color_index], 
        jungle_colors_[next_color_index], 
        color_blend
    );
    
    // Add some subtle color variation based on position for more organic look
    uint32_t pos_seed = static_cast<uint32_t>(position * 1000.0f);
    float color_variation = (simple_random(pos_seed) % 1000) / 1000.0f - 0.5f;
    color_variation *= 0.1f; // Keep variation subtle
    
    // Apply variation to green channel primarily (jungle is mostly green)
    int varied_g = static_cast<int>(base_color.g + color_variation * 30.0f);
    varied_g = std::max(0, std::min(255, varied_g));
    
    return {
        base_color.r,
        static_cast<uint8_t>(varied_g),
        base_color.b
    };
}

void JungleCanopyPattern::update(LEDStrip& strip, uint64_t now_us) {
    float speed = (speed_percent_ <= 0) ? 0.01f : (speed_percent_ / 100.0f);
    float time = (now_us - start_us_) * speed / 1'000'000.0f; // seconds
    
    size_t strip_length = strip.length();
    if (strip_length == 0) return;
    
    for (size_t i = 0; i < strip_length; ++i) {
        // Normalize position to create consistent patterns across different strip lengths
        float position = static_cast<float>(i) / static_cast<float>(strip_length) * 10.0f; // Scale for good wave frequency
        
        // Layer multiple lighting effects to create realistic jungle canopy
        float base_canopy = get_canopy_base_layer(position, time);
        float sunlight_patches = get_sunlight_patches(position, time);
        float leaf_movement = get_leaf_movement(position, time);
        float wind_sway = get_wind_sway(position, time);
        
        // Combine all effects
        // Base canopy provides the foundation
        // Sunlight patches add bright spots
        // Leaf movement adds fine detail
        // Wind sway adds large-scale movement
        float brightness_factor = base_canopy + sunlight_patches * 0.6f + leaf_movement + wind_sway;
        
        // Clamp to reasonable range
        brightness_factor = fmaxf(0.1f, fminf(1.2f, brightness_factor));
        
        // Get the jungle color based on brightness
        JungleColor jungle_color = get_jungle_color(position, time, brightness_factor);
        
        // Apply brightness factor to the color
        uint8_t r = static_cast<uint8_t>(jungle_color.r * brightness_factor);
        uint8_t g = static_cast<uint8_t>(jungle_color.g * brightness_factor);
        uint8_t b = static_cast<uint8_t>(jungle_color.b * brightness_factor);
        
        // Clamp values
        r = std::min(r, static_cast<uint8_t>(255));
        g = std::min(g, static_cast<uint8_t>(255));
        b = std::min(b, static_cast<uint8_t>(255));
        
        // Apply global brightness setting
        if (brightness_percent_ < 100) {
            r = static_cast<uint8_t>((r * brightness_percent_) / 100);
            g = static_cast<uint8_t>((g * brightness_percent_) / 100);
            b = static_cast<uint8_t>((b * brightness_percent_) / 100);
        }
        
        strip.set_pixel(i, r, g, b, 0);
    }
}

} // namespace leds
