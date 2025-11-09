#pragma once

#include "LEDPattern.h"

namespace leds {

class SummaryPattern final : public LEDPattern {
public:
    const char* name() const override { return "SUMMARY"; }
    void reset(LEDStrip& strip, uint64_t now_us) override { (void)now_us; render(strip); }
    void update(LEDStrip& strip, uint64_t now_us) override;
    void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override {
        r_ = r; g_ = g; b_ = b; w_ = w; needs_render_ = true;
    }
    void set_brightness_percent(int bp) override {
        if (bp < 0) bp = 0;
        if (bp > 100) bp = 100;
        brightness_percent_ = bp;
        needs_render_ = true;
    }

private:
    void render(LEDStrip& strip);

    uint8_t r_ = 255, g_ = 255, b_ = 255, w_ = 255;
    int brightness_percent_ = 100;
    int last_key_ = -1; // month*10000 + day*100 + hour24
    bool needs_render_ = false;
};

} // namespace leds




