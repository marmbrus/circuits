#pragma once

#include "LEDPattern.h"
#include <string>

namespace leds {

class SummaryPattern final : public LEDPattern {
public:
    const char* name() const override { return "SUMMARY"; }
    void reset(LEDStrip& strip, uint64_t now_us) override { 
        (void)now_us; 
        last_key_ = -1; // force render
        needs_render_ = true; 
        // Reset marquee state
        scroll_px_ = 0;
        last_step_us_ = 0;
    }
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
    void set_speed_percent(int speed_percent) override {
        if (speed_percent < 0) speed_percent = 0;
        if (speed_percent > 100) speed_percent = 100;
        speed_percent_ = speed_percent;
    }

private:
    void render(LEDStrip& strip);
    void render_summary(LEDStrip& strip);
    void render_countdown(LEDStrip& strip, int seconds_left);
    void render_celebration(LEDStrip& strip);

    uint8_t r_ = 255, g_ = 255, b_ = 255, w_ = 255;
    int brightness_percent_ = 100;
    int last_key_ = -1; // month*10000 + day*100 + hour24
    bool needs_render_ = false;

    // Countdown / Celebration State
    enum class State {
        SUMMARY,
        COUNTDOWN,
        CELEBRATION
    };
    State current_state_ = State::SUMMARY;
    int last_countdown_sec_ = -1;

    // Countdown pulsing border state
    uint64_t last_border_toggle_us_ = 0;
    bool border_on_ = false;

    // Marquee state for celebration
    std::string celebration_msg_;
    int scroll_px_ = 0;
    uint64_t last_step_us_ = 0;
    int speed_percent_ = 50; 

    // Celebration alternating border state
    uint64_t last_celebration_border_toggle_us_ = 0;
    bool celebration_border_state_ = false;
};

} // namespace leds
