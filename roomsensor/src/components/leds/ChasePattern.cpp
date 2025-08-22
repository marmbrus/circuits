#include "ChasePattern.h"
#include "LEDStrip.h"

namespace leds {

void ChasePattern::update(LEDStrip& strip, uint64_t now_us) {
    if (strip.has_enable_pin()) strip.set_power_enabled(true);

    // Determine step interval: map speed 0..100 to 800ms..30ms per LED
    int sp = speed_percent_;
    if (sp < 0) sp = 0;
    if (sp > 100) sp = 100;
    uint64_t step_us = 800000ull - static_cast<uint64_t>(sp) * 7700ull; // ~800ms -> ~30ms
    if (step_us < 20000ull) step_us = 20000ull; // clamp

    size_t n = strip.length();
    if (n == 0) return;

    uint64_t steps = (now_us - start_us_) / step_us;
    size_t idx = static_cast<size_t>(steps % n);

    if (idx != last_idx_) {
        // Clear entire strip and light the one LED at idx
        strip.clear();
        uint8_t r = base_r_, g = base_g_, b = base_b_, w = base_w_;
        if (brightness_percent_ < 100) {
            r = static_cast<uint8_t>((static_cast<uint16_t>(r) * brightness_percent_) / 100);
            g = static_cast<uint8_t>((static_cast<uint16_t>(g) * brightness_percent_) / 100);
            b = static_cast<uint8_t>((static_cast<uint16_t>(b) * brightness_percent_) / 100);
            w = static_cast<uint8_t>((static_cast<uint16_t>(w) * brightness_percent_) / 100);
        }
        strip.set_pixel(idx, r, g, b, w);
        last_idx_ = idx;
    }
}

} // namespace leds



