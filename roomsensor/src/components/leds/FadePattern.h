#pragma once

#include "LEDPattern.h"

namespace leds {

class FadePattern final : public LEDPattern {
public:
    const char* name() const override { return "FADE"; }
    void reset(LEDStrip& strip, uint64_t now_us) override { start_us_ = now_us; }
    void update(LEDStrip& strip, uint64_t now_us) override;
    void set_speed_percent(int speed_percent) override { speed_percent_ = speed_percent; }
    void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override {
        base_r_ = r; base_g_ = g; base_b_ = b; base_w_ = w;
    }

private:
    uint64_t start_us_ = 0;
    int speed_percent_ = 50; // 0..100
    uint8_t base_r_ = 255, base_g_ = 255, base_b_ = 255, base_w_ = 0;
};

} // namespace leds


