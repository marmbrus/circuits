#include "SolidPattern.h"
#include "LEDStrip.h"

namespace leds {

void SolidPattern::reset(LEDStrip& strip, uint64_t /*now_us*/) {
    // Nothing to do; power policy via on_activate
}

void SolidPattern::update(LEDStrip& strip, uint64_t /*now_us*/) {
    if (!color_set_) return; // nothing to draw
    for (size_t i = 0; i < strip.length(); ++i) {
        strip.set_pixel(i, r_, g_, b_, w_);
    }
}

} // namespace leds


