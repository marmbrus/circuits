#include "ColorRotationPattern.h"
#include "LEDStrip.h"
#include <math.h>
#include <algorithm>

namespace leds {

ColorRotationPattern::RGB ColorRotationPattern::hsv_to_rgb(float hue, float saturation, float value) const {
    // Ensure hue is in range [0, 360)
    while (hue < 0.0f) hue += 360.0f;
    while (hue >= 360.0f) hue -= 360.0f;
    
    // Clamp saturation and value to [0, 1]
    saturation = fmaxf(0.0f, fminf(1.0f, saturation));
    value = fmaxf(0.0f, fminf(1.0f, value));
    
    float c = value * saturation;  // Chroma
    float x = c * (1.0f - fabsf(fmodf(hue / 60.0f, 2.0f) - 1.0f));
    float m = value - c;
    
    float r_prime, g_prime, b_prime;
    
    if (hue < 60.0f) {
        r_prime = c; g_prime = x; b_prime = 0.0f;
    } else if (hue < 120.0f) {
        r_prime = x; g_prime = c; b_prime = 0.0f;
    } else if (hue < 180.0f) {
        r_prime = 0.0f; g_prime = c; b_prime = x;
    } else if (hue < 240.0f) {
        r_prime = 0.0f; g_prime = x; b_prime = c;
    } else if (hue < 300.0f) {
        r_prime = x; g_prime = 0.0f; b_prime = c;
    } else {
        r_prime = c; g_prime = 0.0f; b_prime = x;
    }
    
    // Convert to RGB values in range [0, 255]
    uint8_t r = static_cast<uint8_t>((r_prime + m) * 255.0f);
    uint8_t g = static_cast<uint8_t>((g_prime + m) * 255.0f);
    uint8_t b = static_cast<uint8_t>((b_prime + m) * 255.0f);
    
    return {r, g, b};
}

void ColorRotationPattern::update(LEDStrip& strip, uint64_t now_us) {
    // Calculate time-based parameters
    float speed = (speed_percent_ <= 0) ? 0.01f : (speed_percent_ / 100.0f);
    float time = (now_us - start_us_) * speed / 1'000'000.0f; // seconds
    
    size_t strip_length = strip.length();
    if (strip_length == 0) return;
    
    // Calculate the current hue based on time
    // Complete one full rainbow cycle every 10 seconds at 50% speed
    // Adjust cycle time based on speed: faster speed = faster color changes
    float cycle_time = 10.0f / speed; // Base cycle time adjusted by speed
    float hue = fmodf(time * 360.0f / cycle_time, 360.0f);
    
    // Use full saturation and value for vibrant colors
    float saturation = 1.0f;
    float value = 1.0f;
    
    // Convert HSV to RGB
    RGB color = hsv_to_rgb(hue, saturation, value);
    
    // Apply global brightness setting
    uint8_t r = color.r;
    uint8_t g = color.g;
    uint8_t b = color.b;
    
    if (brightness_percent_ < 100) {
        r = static_cast<uint8_t>((r * brightness_percent_) / 100);
        g = static_cast<uint8_t>((g * brightness_percent_) / 100);
        b = static_cast<uint8_t>((b * brightness_percent_) / 100);
    }
    
    // Set all LEDs to the same color
    for (size_t i = 0; i < strip_length; ++i) {
        strip.set_pixel(i, r, g, b, 0);
    }
}

} // namespace leds
