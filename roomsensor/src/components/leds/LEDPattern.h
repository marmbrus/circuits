#pragma once

#include <cstdint>

namespace leds {

class LEDStrip; // forward declaration

// Abstract base for all LED animation patterns.
// A pattern receives the current real time (in microseconds) on each update
// and may update the provided LEDStrip. Implementations should:
// - Use real elapsed time (now_us) rather than frame counters to ensure smooth motion
//   even when frames are skipped due to RMT backpressure.
// - Minimize writes; call LEDStrip methods only when pixel values actually change.
// - Be re-entrant across strips; do not use global mutable state.
class LEDPattern {
public:
    virtual ~LEDPattern() = default;

    // A short, stable name for diagnostics and JSON; does not need to match config string exactly.
    virtual const char* name() const = 0;

    // Called when the pattern is installed on a strip or when configuration affecting the pattern changes.
    // Implementations should capture any per-strip, per-instance state here.
    virtual void reset(LEDStrip& strip, uint64_t now_us) {}

    // Advance pattern state to current time and write any changed pixels to the strip.
    // Implementations should avoid redundant writes; LEDStrip will internally track a dirty bit.
    virtual void update(LEDStrip& strip, uint64_t now_us) = 0;

    // Optional runtime knobs that some patterns may honor. Defaults are no-ops.
    virtual void set_speed_percent(int speed_percent) {}
    // Note: Brightness is pattern-specific, not a global strip property. Patterns may implement dimming
    // by subsampling LEDs and/or scaling color channels as appropriate for the effect.
    virtual void set_brightness_percent(int brightness_percent) {}
    virtual void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) {}
};

} // namespace leds


