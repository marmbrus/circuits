#pragma once

#include "LEDPattern.h"
#include <vector>
#include <string>

namespace leds {

class GameOfLifePattern final : public LEDPattern {
public:
    const char* name() const override { return "LIFE"; }
    void reset(LEDStrip& strip, uint64_t now_us) override;
    void update(LEDStrip& strip, uint64_t now_us) override;

    void set_speed_percent(int speed_percent) override { speed_percent_ = speed_percent; }
    void set_brightness_percent(int brightness_percent) override {
        if (brightness_percent < 0) brightness_percent = 0;
        if (brightness_percent > 100) brightness_percent = 100;
        brightness_percent_ = brightness_percent;
    }
    void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override {
        // If all zeros provided (typical when not configured), keep existing defaults
        if ((r | g | b | w) != 0) { base_r_ = r; base_g_ = g; base_b_ = b; base_w_ = w; }
    }
    void set_start_string(const char* start) override { start_string_ = start ? start : ""; }

private:
    void randomize_state(size_t rows, size_t cols, uint32_t seed);
    unsigned count_live_neighbors(size_t rows, size_t cols, size_t r, size_t c) const;
    void render_current(LEDStrip& strip) const;

    std::vector<uint8_t> current_; // 0 or 1 per cell
    std::vector<uint8_t> next_;
    std::vector<uint8_t> prev1_;
    std::vector<uint8_t> prev2_;
    uint64_t last_step_us_ = 0;
    uint64_t repeat_start_us_ = 0;
    int speed_percent_ = 50; // 0..100
    int brightness_percent_ = 100; // 0..100
    uint8_t base_r_ = 255, base_g_ = 255, base_b_ = 255, base_w_ = 0;
    std::string start_string_;
    bool simple_mode_ = false;
};

} // namespace leds



