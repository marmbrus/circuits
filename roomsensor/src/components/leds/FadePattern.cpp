#include "FadePattern.h"
#include "LEDStrip.h"
#include <cmath>

namespace leds {

void FadePattern::update(LEDStrip& strip, uint64_t now_us) {
    if (strip.has_enable_pin()) strip.set_power_enabled(true);
    // Simple sine fade over 2 seconds scaled by speed_percent
    float speed = (speed_percent_ <= 0) ? 0.01f : (speed_percent_ / 100.0f);
    float phase = fmodf((now_us - start_us_) * speed / 2'000'000.0f, 1.0f);
    float s = 0.5f * (1.0f + sinf(phase * 2.0f * 3.1415926535f));
    uint8_t r = static_cast<uint8_t>(base_r_ * s);
    uint8_t g = static_cast<uint8_t>(base_g_ * s);
    uint8_t b = static_cast<uint8_t>(base_b_ * s);
    uint8_t w = static_cast<uint8_t>(base_w_ * s);
    for (size_t i = 0; i < strip.length(); ++i) {
        strip.set_pixel(i, r, g, b, w);
    }
}

} // namespace leds


