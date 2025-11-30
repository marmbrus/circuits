#pragma once

#include "LEDPattern.h"
#include <cstddef>

namespace leds {

// Similar to CrossWipe, but line by line transitions the grid from all ON to all OFF
// (by rows) then from all OFF to all ON (by columns), and repeats.
class CrossFadePattern final : public LEDPattern {
public:
    const char* name() const override { return "CROSS_FADE"; }
    void reset(LEDStrip& strip, uint64_t now_us) override;
    void update(LEDStrip& strip, uint64_t now_us) override;

    // Duration in seconds for a full row or column sweep; 0 => 1s.
    void set_speed_percent(int speed_seconds) override;
    void set_brightness_percent(int brightness_percent) override;
    void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override;

private:
    enum class Phase {
        ROW_DOWN, // rows turn OFF from top to bottom
        COL_UP,   // columns turn ON from left to right
    };

    // Config/state
    uint8_t r_ = 255, g_ = 255, b_ = 255, w_ = 0;
    bool color_set_ = false;

    int duration_seconds_ = 2;    // per sweep
    int brightness_percent_ = 100;

    size_t rows_ = 1;
    size_t cols_ = 0;

    Phase phase_ = Phase::ROW_DOWN;
    uint64_t phase_start_us_ = 0;

    // Helpers
    uint64_t phase_duration_us() const;
};

} // namespace leds




