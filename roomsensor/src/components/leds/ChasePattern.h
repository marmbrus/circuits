#pragma once

#include "LEDPattern.h"
#include <cstddef>

namespace leds {

class ChasePattern final : public LEDPattern {
public:
    const char* name() const override { return "CHASE"; }
    void reset(LEDStrip& strip, uint64_t now_us) override {
        (void)strip;
        start_us_ = now_us;
        last_idx_ = static_cast<size_t>(-1);
    }
    void update(LEDStrip& strip, uint64_t now_us) override;

    void set_speed_percent(int speed_percent) override { speed_percent_ = speed_percent; }
    void set_brightness_percent(int brightness_percent) override {
        if (brightness_percent < 0) brightness_percent = 0;
        if (brightness_percent > 100) brightness_percent = 100;
        brightness_percent_ = brightness_percent;
    }
    void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override {
        // Keep default white when not configured
        if ((r | g | b | w) != 0) { base_r_ = r; base_g_ = g; base_b_ = b; base_w_ = w; }
    }

private:
    uint64_t start_us_ = 0;
    int speed_percent_ = 50; // 0..100
    int brightness_percent_ = 100; // 0..100
    uint8_t base_r_ = 255, base_g_ = 255, base_b_ = 255, base_w_ = 0;
    size_t last_idx_ = static_cast<size_t>(-1);
};

} // namespace leds



