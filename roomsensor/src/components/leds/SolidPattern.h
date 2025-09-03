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
    void set_brightness_percent(int brightness_percent) override {
        if (brightness_percent < 0) brightness_percent = 0;
        if (brightness_percent > 100) brightness_percent = 100;
        brightness_percent_ = brightness_percent;
    }

private:
    bool color_set_ = false;
    uint8_t r_ = 0, g_ = 0, b_ = 0, w_ = 0;
    int brightness_percent_ = 100;
    uint64_t pwm_anchor_us_ = 0;
};

} // namespace leds


