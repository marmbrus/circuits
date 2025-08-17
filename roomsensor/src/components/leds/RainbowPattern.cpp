#include "RainbowPattern.h"
#include "LEDStrip.h"
#include <math.h>

namespace leds {

static void hsv_to_rgb(float h, float s, float v, uint8_t& r, uint8_t& g, uint8_t& b) {
    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
    float m = v - c;
    float rf, gf, bf;
    if (h < 60) { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else { rf = c; gf = 0; bf = x; }
    r = static_cast<uint8_t>((rf + m) * 255);
    g = static_cast<uint8_t>((gf + m) * 255);
    b = static_cast<uint8_t>((bf + m) * 255);
}

void RainbowPattern::update(LEDStrip& strip, uint64_t now_us) {
    if (strip.has_enable_pin()) strip.set_power_enabled(true);
    float speed = (speed_percent_ <= 0) ? 0.01f : (speed_percent_ / 100.0f);
    float t = (now_us - start_us_) * speed / 1'000'000.0f; // seconds
    for (size_t i = 0; i < strip.length(); ++i) {
        float hue = fmodf((i * 360.0f / strip.length()) + (t * 60.0f), 360.0f);
        uint8_t r, g, b;
        hsv_to_rgb(hue, 1.0f, 1.0f, r, g, b);
        if (brightness_percent_ < 100) {
            r = static_cast<uint8_t>((r * brightness_percent_) / 100);
            g = static_cast<uint8_t>((g * brightness_percent_) / 100);
            b = static_cast<uint8_t>((b * brightness_percent_) / 100);
        }
        strip.set_pixel(i, r, g, b, 0);
    }
}

} // namespace leds


