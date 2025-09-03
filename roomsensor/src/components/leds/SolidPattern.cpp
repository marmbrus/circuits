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
    // Even spacing using accumulator (Bresenham-like): distribute on_count ON pixels across total positions
    size_t acc = 0;
    for (size_t i = 0; i < total; ++i) {
        acc += on_count;
        bool on = false;
        if (acc >= total) { on = true; acc -= total; }
        if (on) strip.set_pixel(i, r_, g_, b_, w_);
        else strip.set_pixel(i, 0, 0, 0, 0);
    }
}

} // namespace leds


