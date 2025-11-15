#include "AuroraPattern.h"
#include "LEDStrip.h"
#include <math.h>

namespace leds {

constexpr AuroraPattern::AuroraColor AuroraPattern::aurora_colors_[];

float AuroraPattern::get_brightness_factor(float position, float time) const {
    // Create complex, organic-looking brightness variations using multiple sine waves
    // with different frequencies and phases to mimic the natural aurora movement
    
    // Primary brightness waves with more dramatic amplitude
    float wave1 = sinf(position * 0.4f + time * 0.25f) * 0.6f;  // Slower, more dramatic
    float wave2 = sinf(position * 1.1f + time * 0.6f) * 0.4f;   // Medium frequency
    float wave3 = sinf(position * 2.3f + time * 0.15f) * 0.3f;  // Fine detail
    
    // Add moving brightness "hotspots" that travel across the strip
    float hotspot1 = sinf(position * 0.8f - time * 0.4f) * 0.5f;  // Moving left to right
    float hotspot2 = sinf(position * 1.5f + time * 0.3f) * 0.4f;  // Moving right to left
    
    // Create "breathing" effect - overall brightness that pulses
    float breathing = sinf(time * 0.8f) * 0.2f;
    
    // Combine all effects
    float combined = wave1 + wave2 + wave3 + hotspot1 + hotspot2 + breathing;
    
    // Apply non-linear scaling to create more dramatic bright/dim contrast
    // Use a power function to make bright areas brighter and dim areas dimmer
    float normalized = 0.05f + (combined + 2.0f) * 0.475f; // Maps to [0.05, 1.0] range
    
    // Apply power curve for more dramatic contrast (bright areas get brighter, dim areas get dimmer)
    float contrast_enhanced = powf(normalized, 1.8f);
    
    // Ensure we stay within valid range and maintain some minimum brightness
    return fmaxf(0.02f, fminf(1.0f, contrast_enhanced));
}

AuroraPattern::AuroraColor AuroraPattern::interpolate_color(const AuroraColor& c1, const AuroraColor& c2, float t) const {
    if (t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;
    
    return {
        static_cast<uint8_t>(c1.r + (c2.r - c1.r) * t),
        static_cast<uint8_t>(c1.g + (c2.g - c1.g) * t),
        static_cast<uint8_t>(c1.b + (c2.b - c1.b) * t)
    };
}

void AuroraPattern::update(LEDStrip& strip, uint64_t now_us) {
    float speed = (speed_percent_ <= 0) ? 0.01f : (speed_percent_ / 100.0f);
    float time = (now_us - start_us_) * speed / 1'000'000.0f; // seconds
    
    size_t strip_length = strip.length();
    if (strip_length == 0) return;
    
    for (size_t i = 0; i < strip_length; ++i) {
        // Normalize position to 0-1 range
        float position = static_cast<float>(i) / static_cast<float>(strip_length);
        
        // Create slowly moving color zones across the strip
        // The color selection moves slowly across the strip over time
        // Use a full cycle that wraps seamlessly by mapping to the full color palette range
        float color_position = fmodf(position * 2.0f + time * 0.1f, 1.0f);
        
        // Map to color palette with smooth transitions that wrap around seamlessly
        // Scale by num_colors_ (not num_colors_ - 1) to ensure smooth wrapping
        float color_index_f = color_position * static_cast<float>(num_colors_);
        size_t color_index = static_cast<size_t>(floorf(color_index_f)) % num_colors_;
        size_t next_color_index = (color_index + 1) % num_colors_;
        float color_blend = color_index_f - floorf(color_index_f);
        
        // Interpolate between adjacent colors in the palette
        AuroraColor base_color = interpolate_color(
            aurora_colors_[color_index], 
            aurora_colors_[next_color_index], 
            color_blend
        );
        
        // Apply brightness variation to create the characteristic aurora "curtain" effect
        float brightness_factor = get_brightness_factor(position * 10.0f, time);
        
        // Apply brightness scaling
        uint8_t r = static_cast<uint8_t>(base_color.r * brightness_factor);
        uint8_t g = static_cast<uint8_t>(base_color.g * brightness_factor);
        uint8_t b = static_cast<uint8_t>(base_color.b * brightness_factor);
        
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
