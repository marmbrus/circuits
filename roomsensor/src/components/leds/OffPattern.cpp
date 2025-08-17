#include "OffPattern.h"
#include "LEDStrip.h"

namespace leds {

void OffPattern::reset(LEDStrip& strip, uint64_t /*now_us*/) {
    strip.clear();
}

void OffPattern::update(LEDStrip& strip, uint64_t /*now_us*/) {
    // Ensure power is off every tick; cheap and robust
    if (strip.has_enable_pin()) strip.set_power_enabled(false);
    strip.clear();
}

} // namespace leds


