#include "SolidPattern.h"
#include "LEDStrip.h"

namespace leds {

void SolidPattern::reset(LEDStrip& strip, uint64_t /*now_us*/) {
    // Nothing to do; power policy via on_activate
}

void SolidPattern::update(LEDStrip& strip, uint64_t now_us) {
    if (!color_set_) return; // nothing to draw

    // Spatial duty: below 100%, leave exactly K LEDs ON, spaced as evenly as possible
    if (brightness_percent_ <= 0) {
        for (size_t i = 0; i < strip.length(); ++i) strip.set_pixel(i, 0, 0, 0, 0);
        return;
    }
    if (brightness_percent_ >= 100) {
        for (size_t i = 0; i < strip.length(); ++i) strip.set_pixel(i, r_, g_, b_, w_);
        return;
    }
    size_t total = strip.length();
    size_t on_count = (total * static_cast<size_t>(brightness_percent_)) / 100u; // 1..total-1
    if (on_count == 0) {
        for (size_t i = 0; i < total; ++i) strip.set_pixel(i, 0, 0, 0, 0);
        return;
    }
    if (on_count >= total) {
        for (size_t i = 0; i < total; ++i) strip.set_pixel(i, r_, g_, b_, w_);
        return;
    }
    // Advance chase offset as fast as allowed; speed is LEDs per second, 0 = advance every frame
    bool should_advance = false;
    if (speed_percent_ <= 0) {
        should_advance = false; // no movement
    } else if (speed_percent_ >= 100) {
        should_advance = true; // as fast as possible (each tick)
    } else {
        // Map 1..99% to a min interval from 200ms..10ms (slower at low %, faster near 100%)
        // approx: interval_us = 200ms - ((speed%/100)*(190ms))
        uint64_t max_us = 200'000ULL;
        uint64_t min_us = 10'000ULL;
        uint64_t span = max_us - min_us;
        uint64_t interval_us = max_us - (span * static_cast<uint64_t>(speed_percent_) / 100ULL);
        if (interval_us < min_us) interval_us = min_us;
        if (last_advance_us_ == 0 || (now_us - last_advance_us_) >= interval_us) should_advance = true;
    }
    if (should_advance) {
        chase_offset_ = (chase_offset_ + 1) % ((total == 0) ? 1 : total);
        last_advance_us_ = now_us;
    }

    // Even spacing with offset (Bresenham-like) to allow chasing
    size_t acc = 0;
    for (size_t i = 0; i < total; ++i) {
        size_t pos = (i + chase_offset_) % total;
        acc += on_count;
        bool on = false;
        if (acc >= total) { on = true; acc -= total; }
        if (on) strip.set_pixel(pos, r_, g_, b_, w_);
        else strip.set_pixel(pos, 0, 0, 0, 0);
    }
}

} // namespace leds


