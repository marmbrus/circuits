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

private:
    bool color_set_ = false;
    uint8_t r_ = 0, g_ = 0, b_ = 0, w_ = 0;
};

} // namespace leds


