#pragma once

#include "LEDPattern.h"
#include <string>

namespace leds {

// Simple text marquee that scrolls a configured message from right to left.
// Uses the shared font6x6 glyph renderer and scrolls smoothly at pixel
// granularity (advancing at most one pixel per update tick).
class MarqueePattern final : public LEDPattern {
public:
    const char* name() const override { return "MARQUEE"; }

    void reset(LEDStrip& strip, uint64_t now_us) override;
    void update(LEDStrip& strip, uint64_t now_us) override;

    void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override;
    void set_brightness_percent(int brightness_percent) override;
    void set_speed_percent(int speed_percent) override;
    void set_start_string(const char* start) override;

private:
    void render(LEDStrip& strip);

    std::string message_{"HELLO"};

    uint8_t r_ = 255, g_ = 255, b_ = 255, w_ = 0;
    int brightness_percent_ = 100;   // 0..100
    int speed_percent_ = 50;        // 0..100, maps to step interval

    // Scroll position measured in pixels along the marquee cycle.
    int scroll_px_ = 0;
    uint64_t last_step_us_ = 0;
};

} // namespace leds




