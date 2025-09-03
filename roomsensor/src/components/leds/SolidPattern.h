#pragma once

#include "LEDPattern.h"
#include <cstddef>

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
    void set_speed_percent(int speed_percent) override {
        if (speed_percent < 0) speed_percent = 0;
        if (speed_percent > 100) speed_percent = 100;
        speed_percent_ = speed_percent; // 0=no move, 100=advance every update
    }

private:
    bool color_set_ = false;
    uint8_t r_ = 0, g_ = 0, b_ = 0, w_ = 0;
    int brightness_percent_ = 100;
    int speed_percent_ = 100; // 0..100
    uint64_t last_advance_us_ = 0;
    size_t chase_offset_ = 0;
};

} // namespace leds


