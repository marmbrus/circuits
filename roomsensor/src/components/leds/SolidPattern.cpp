#include "SolidPattern.h"
#include "LEDStrip.h"

namespace leds {

void SolidPattern::reset(LEDStrip& strip, uint64_t /*now_us*/) {
    // Nothing to do; power policy via on_activate
    last_on_state_valid_ = false;
}

void SolidPattern::update(LEDStrip& strip, uint64_t now_us) {
    if (strip.has_enable_pin()) strip.set_power_enabled(true);
    if (!color_set_) return; // nothing to draw
    // Temporal PWM using a fixed period; compute on/off state from now_us and duty
    if (duty_percent_ <= 0) {
        if (!last_on_state_valid_ || last_on_state_) {
            strip.clear();
            last_on_state_ = false;
            last_on_state_valid_ = true;
        }
        return;
    }
    if (duty_percent_ >= 100) {
        if (!last_on_state_valid_ || !last_on_state_) {
            for (size_t i = 0; i < strip.length(); ++i) strip.set_pixel(i, r_, g_, b_, w_);
            last_on_state_ = true;
            last_on_state_valid_ = true;
        }
        return;
    }

    const uint64_t phase = now_us % pwm_period_us_;
    const uint64_t on_window_us = (pwm_period_us_ * static_cast<uint64_t>(duty_percent_)) / 100ull;
    const bool on_now = phase < on_window_us;

    if (!last_on_state_valid_ || on_now != last_on_state_) {
        if (on_now) {
            for (size_t i = 0; i < strip.length(); ++i) strip.set_pixel(i, r_, g_, b_, w_);
        } else {
            strip.clear();
        }
        last_on_state_ = on_now;
        last_on_state_valid_ = true;
    }
}

} // namespace leds


