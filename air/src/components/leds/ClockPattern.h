#pragma once

#include "LEDPattern.h"
#include <cstddef>
#include <array>

namespace leds {

class ClockPattern final : public LEDPattern {
public:
    const char* name() const override { return "CLOCK"; }
    void reset(LEDStrip& strip, uint64_t now_us) override { (void)now_us; render(strip); }
    void update(LEDStrip& strip, uint64_t now_us) override;
    void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override {
        r_ = r; g_ = g; b_ = b; w_ = w; needs_render_ = true;
    }
    void set_brightness_percent(int brightness_percent) override {
        if (brightness_percent < 0) brightness_percent = 0;
        if (brightness_percent > 100) brightness_percent = 100;
        brightness_percent_ = brightness_percent;
        needs_render_ = true;
    }

private:
    void render(LEDStrip& strip);
    void draw_digit(LEDStrip& strip, size_t top_row, size_t left_col, int digit,
                    uint8_t rr, uint8_t gg, uint8_t bb, uint8_t ww);
    void draw_outline(LEDStrip& strip);

    // Track last drawn outline snake to erase cleanly next tick
    static constexpr int kSnakeLen = 4;
    std::array<size_t, kSnakeLen> last_snake_idx_{};
    int last_snake_count_ = 0;

    uint8_t r_ = 255, g_ = 255, b_ = 255, w_ = 255;
    int last_rendered_min_ = -1;
    int brightness_percent_ = 100;
    bool needs_render_ = false;
};

} // namespace leds



