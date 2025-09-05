#pragma once

#include "LEDPattern.h"

namespace leds {

class PositionTestPattern final : public LEDPattern {
public:
    const char* name() const override { return "POSITION"; }
    void reset(LEDStrip& strip, uint64_t now_us) override { (void)now_us; update(strip, now_us); }
    void update(LEDStrip& strip, uint64_t now_us) override;
    void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override {
        (void)b; (void)w; r_ = r; g_ = g;
    }

private:
    uint8_t r_ = 0;
    uint8_t g_ = 0;
};

} // namespace leds


