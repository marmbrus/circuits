#pragma once

#include "LEDPattern.h"

namespace leds {

class FadePattern final : public LEDPattern {
public:
    const char* name() const override { return "FADE"; }
    void reset(LEDStrip& strip, uint64_t now_us) override { (void)strip; fade_start_us_ = now_us; initialized_ = true; }
    void update(LEDStrip& strip, uint64_t now_us) override;
    // Interpret speed as duration in seconds: 0 => immediate, 1 => 1s, 60 => 1 minute
    void set_speed_percent(int speed_seconds) override { duration_seconds_ = (speed_seconds < 0) ? 0 : speed_seconds; }
    void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override { target_r_ = r; target_g_ = g; target_b_ = b; target_w_ = w; target_dirty_ = true; }
    void set_brightness_percent(int brightness_percent) override {
        if (brightness_percent < 0) brightness_percent = 0;
        if (brightness_percent > 100) brightness_percent = 100;
        target_brightness_percent_ = brightness_percent;
        target_dirty_ = true; // brightness change should also trigger a fade segment
    }

private:
    // Fade state
    uint64_t fade_start_us_ = 0; // when current segment began
    int duration_seconds_ = 1;   // duration for full transition
    bool initialized_ = false;
    bool target_dirty_ = false;

    // Colors for current segment
    uint8_t start_r_ = 0, start_g_ = 0, start_b_ = 0, start_w_ = 0;
    uint8_t target_r_ = 0, target_g_ = 0, target_b_ = 0, target_w_ = 0;
    uint8_t last_out_r_ = 0, last_out_g_ = 0, last_out_b_ = 0, last_out_w_ = 0;

    // Spatial brightness duty (evenly spaced on LEDs) with fade between levels
    int start_brightness_percent_ = 100;
    int target_brightness_percent_ = 100;
    int last_out_brightness_percent_ = 100;
};

} // namespace leds


