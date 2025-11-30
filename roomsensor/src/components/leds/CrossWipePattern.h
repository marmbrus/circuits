#pragma once

#include "LEDPattern.h"
#include <cstddef>

namespace leds {

// Repeatedly wipes a single row and then a single column across the grid.
// Uses the configured solid color (default white) and an optional brightness scaler.
class CrossWipePattern final : public LEDPattern {
public:
    const char* name() const override { return "CROSS_WIPE"; }
    void reset(LEDStrip& strip, uint64_t now_us) override;
    void update(LEDStrip& strip, uint64_t now_us) override;

    // Interpret speed as duration in seconds for a full row or column sweep:
    // 0 => 1s, 1 => 1s, 5 => 5s, etc.
    void set_speed_percent(int speed_seconds) override;
    void set_brightness_percent(int brightness_percent) override;
    void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override;

private:
    enum class Phase {
        ROW,
        COL,
    };

    // Config/state
    uint8_t r_ = 255, g_ = 255, b_ = 255, w_ = 0;
    bool color_set_ = false;

    int duration_seconds_ = 2;    // per-sweep duration
    int brightness_percent_ = 100;

    size_t rows_ = 1;
    size_t cols_ = 0;

    Phase phase_ = Phase::ROW;
    uint64_t phase_start_us_ = 0;

    // Helpers
    uint64_t phase_duration_us() const;
};

} // namespace leds




