#include "UnderseaGrottoPattern.h"
#include "LEDStrip.h"
#include <math.h>
#include <algorithm>

namespace leds {

constexpr UnderseaGrottoPattern::OceanColor UnderseaGrottoPattern::ocean_colors_[];

uint32_t UnderseaGrottoPattern::simple_random(uint32_t seed) const {
    // Simple linear congruential generator for organic water variation
    return seed * 1103515245 + 12345;
}

float UnderseaGrottoPattern::get_water_depth_layer(float position, float time) const {
    // Create the base water depth effect - deeper areas are darker
    // This represents the fundamental light attenuation through water
    
    float depth_wave1 = sinf(position * 0.2f + time * 0.05f) * 0.25f;
    float depth_wave2 = sinf(position * 0.4f + time * 0.08f) * 0.15f;
    float depth_wave3 = sinf(position * 0.6f + time * 0.03f) * 0.1f;
    
    // Combine waves to create varying water depth/clarity
    float combined = depth_wave1 + depth_wave2 + depth_wave3;
    
    // Map to 0.3 - 0.9 range (underwater is never completely bright)
    return 0.6f + combined;
}

float UnderseaGrottoPattern::get_wave_motion(float position, float time) const {
    // Simulate the gentle pulsing motion of waves above
    // This creates the characteristic underwater light fluctuation
    
    // Large, slow wave motion (primary wave effect)
    float wave1 = sinf(position * 0.3f + time * 0.4f) * 0.3f;
    float wave2 = sinf(position * 0.5f - time * 0.3f) * 0.2f;
    
    // Medium frequency waves (secondary wave patterns)
    float wave3 = sinf(position * 0.8f + time * 0.6f) * 0.15f;
    float wave4 = sinf(position * 1.2f - time * 0.5f) * 0.1f;
    
    // Combine all wave motions
    return wave1 + wave2 + wave3 + wave4;
}

float UnderseaGrottoPattern::get_surface_light_rays(float position, float time) const {
    // Create rays of light filtering down from the surface
    // These should be less frequent but more dramatic, like sunbeams through water
    
    // Large light rays that move slowly across the ocean floor
    float ray1 = sinf(position * 0.25f - time * 0.1f) * 0.4f;
    float ray2 = sinf(position * 0.4f + time * 0.12f) * 0.3f;
    
    // Smaller, more frequent light variations
    float shimmer1 = sinf(position * 1.1f - time * 0.8f) * 0.15f;
    float shimmer2 = sinf(position * 1.6f + time * 0.7f) * 0.1f;
    
    // Combine and apply threshold to create distinct light rays
    float combined = ray1 + ray2 + shimmer1 + shimmer2;
    
    // Use a softer power function than jungle canopy for more gentle underwater effect
    float normalized = (combined + 1.0f) * 0.5f; // Map to 0-1
    return powf(normalized, 1.8f); // Gentler contrast than jungle
}

float UnderseaGrottoPattern::get_water_turbulence(float position, float time) const {
    // Simulate small water turbulence and particle movement
    // High frequency, very low amplitude variations
    
    float turbulence1 = sinf(position * 4.2f + time * 1.2f) * 0.05f;
    float turbulence2 = sinf(position * 6.1f - time * 0.9f) * 0.04f;
    float turbulence3 = sinf(position * 8.3f + time * 1.5f) * 0.03f;
    float turbulence4 = sinf(position * 10.7f - time * 1.1f) * 0.02f;
    
    return turbulence1 + turbulence2 + turbulence3 + turbulence4;
}

float UnderseaGrottoPattern::get_gentle_current(float position, float time) const {
    // Large-scale gentle current motion that affects the entire underwater scene
    // Very low frequency, medium amplitude
    
    float current1 = sinf(position * 0.15f + time * 0.2f) * 0.2f;
    float current2 = sinf(position * 0.25f - time * 0.15f) * 0.15f;
    
    return current1 + current2;
}

UnderseaGrottoPattern::OceanColor UnderseaGrottoPattern::interpolate_color(const OceanColor& c1, const OceanColor& c2, float t) const {
    if (t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;
    
    return {
        static_cast<uint8_t>(c1.r + (c2.r - c1.r) * t),
        static_cast<uint8_t>(c1.g + (c2.g - c1.g) * t),
        static_cast<uint8_t>(c1.b + (c2.b - c1.b) * t)
    };
}

UnderseaGrottoPattern::OceanColor UnderseaGrottoPattern::get_ocean_color(float position, float time, float depth_brightness) const {
    // Select base color based on depth brightness - deeper areas use darker blues
    float color_position = depth_brightness; // Use brightness to drive color selection
    
    // Map brightness to color palette position
    float color_index_f = color_position * static_cast<float>(num_colors_ - 1);
    size_t color_index = static_cast<size_t>(floorf(color_index_f));
    color_index = std::min(color_index, num_colors_ - 1);
    size_t next_color_index = std::min(color_index + 1, num_colors_ - 1);
    float color_blend = color_index_f - floorf(color_index_f);
    
    // Interpolate between colors
    OceanColor base_color = interpolate_color(
        ocean_colors_[color_index], 
        ocean_colors_[next_color_index], 
        color_blend
    );
    
    // Add some subtle color variation based on position for more organic underwater look
    uint32_t pos_seed = static_cast<uint32_t>(position * 1000.0f + time * 100.0f);
    float color_variation = (simple_random(pos_seed) % 1000) / 1000.0f - 0.5f;
    color_variation *= 0.08f; // Keep variation very subtle for underwater effect
    
    // Apply variation primarily to blue channel (underwater is mostly blue)
    int varied_b = static_cast<int>(base_color.b + color_variation * 20.0f);
    varied_b = std::max(0, std::min(255, varied_b));
    
    // Also add slight variation to green for more realistic water color
    int varied_g = static_cast<int>(base_color.g + color_variation * 10.0f);
    varied_g = std::max(0, std::min(255, varied_g));
    
    return {
        base_color.r,
        static_cast<uint8_t>(varied_g),
        static_cast<uint8_t>(varied_b)
    };
}

void UnderseaGrottoPattern::update(LEDStrip& strip, uint64_t now_us) {
    float speed = (speed_percent_ <= 0) ? 0.01f : (speed_percent_ / 100.0f);
    float time = (now_us - start_us_) * speed / 1'000'000.0f; // seconds
    
    size_t strip_length = strip.length();
    if (strip_length == 0) return;
    
    for (size_t i = 0; i < strip_length; ++i) {
        // Normalize position to create consistent underwater patterns
        float position = static_cast<float>(i) / static_cast<float>(strip_length) * 8.0f; // Scale for good wave frequency
        
        // Layer multiple underwater lighting effects
        float water_depth = get_water_depth_layer(position, time);
        float wave_motion = get_wave_motion(position, time);
        float surface_rays = get_surface_light_rays(position, time);
        float turbulence = get_water_turbulence(position, time);
        float current = get_gentle_current(position, time);
        
        // Combine all effects with appropriate weighting for underwater scene
        // Water depth provides the foundation
        // Wave motion adds the characteristic underwater pulsing
        // Surface rays add occasional bright spots (filtered sunlight)
        // Turbulence adds fine detail
        // Current adds large-scale gentle movement
        float depth_brightness = water_depth + wave_motion * 0.4f + surface_rays * 0.3f + turbulence + current;
        
        // Clamp to underwater-appropriate range (never too bright)
        depth_brightness = fmaxf(0.1f, fminf(1.0f, depth_brightness));
        
        // Get the ocean color based on depth brightness
        OceanColor ocean_color = get_ocean_color(position, time, depth_brightness);
        
        // Apply depth brightness factor to the color
        uint8_t r = static_cast<uint8_t>(ocean_color.r * depth_brightness);
        uint8_t g = static_cast<uint8_t>(ocean_color.g * depth_brightness);
        uint8_t b = static_cast<uint8_t>(ocean_color.b * depth_brightness);
        
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
