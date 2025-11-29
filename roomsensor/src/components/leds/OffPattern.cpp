#include "OffPattern.h"
#include "LEDStrip.h"

namespace leds {

void OffPattern::reset(LEDStrip& strip, uint64_t /*now_us*/) {
    strip.clear();
}

void OffPattern::update(LEDStrip& strip, uint64_t /*now_us*/) {
    // No-op during steady state.
    // We already cleared the strip in reset(), and LEDManager will still
    // periodically call flush_if_dirty() with a long max_quiescent_us window
    // to recover from any transient glitches. Repeated clears here would mark
    // the strip dirty every tick and cause continuous transmissions even when
    // the pattern is logically OFF.
    (void)strip;
}

} // namespace leds


