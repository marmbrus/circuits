#pragma once

#include "LEDPattern.h"

namespace leds {

class RainbowPattern final : public LEDPattern {
public:
    const char* name() const override { return "RAINBOW"; }
    void reset(LEDStrip& strip, uint64_t now_us) override { start_us_ = now_us; }
    void update(LEDStrip& strip, uint64_t now_us) override;
    void set_speed_percent(int speed_percent) override { speed_percent_ = speed_percent; }
    void set_brightness_percent(int brightness_percent) override {
        if (brightness_percent < 0) brightness_percent = 0;
        if (brightness_percent > 100) brightness_percent = 100;
        brightness_percent_ = brightness_percent;
    }

private:
    uint64_t start_us_ = 0;
    int speed_percent_ = 50; // 0..100
    int brightness_percent_ = 100; // 0..100
};

} // namespace leds


