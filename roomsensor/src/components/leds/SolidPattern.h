#pragma once

#include "LEDPattern.h"

namespace leds {

class SolidPattern final : public LEDPattern {
public:
    const char* name() const override { return "SOLID"; }
    void reset(LEDStrip& strip, uint64_t now_us) override;
    void update(LEDStrip& strip, uint64_t now_us) override;

    void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override {
        r_ = r; g_ = g; b_ = b; w_ = w; color_set_ = true;
    }

    void set_duty_percent(int duty_percent) override {
        if (duty_percent < 0) duty_percent = 0;
        if (duty_percent > 100) duty_percent = 100;
        duty_percent_ = duty_percent;
        last_on_state_valid_ = false; // force recompute and redraw next update
    }

private:
    bool color_set_ = false;
    uint8_t r_ = 0, g_ = 0, b_ = 0, w_ = 0;
    int duty_percent_ = 100; // 0..100, default fully on
    // Temporal PWM state
    bool last_on_state_valid_ = false;
    bool last_on_state_ = false;
    uint64_t pwm_period_us_ = 10'000; // ~100 Hz; matches update cadence (5 ms) to minimize flicker
};

} // namespace leds


