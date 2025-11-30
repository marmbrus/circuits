#pragma once

#include "LEDPattern.h"
#include <cstddef>
#include <vector>

namespace leds {

// Fireworks: rockets launch from the "bottom", arc upward, then explode into sparks.
// Works on both 2D grids and 1D strips by treating the longest axis as vertical height.
class FireworksPattern final : public LEDPattern {
public:
    const char* name() const override { return "FIREWORKS"; }
    void reset(LEDStrip& strip, uint64_t now_us) override;
    void update(LEDStrip& strip, uint64_t now_us) override;

    // Interpret speed as approximate seconds for a full firework (launch + fade).
    void set_speed_percent(int speed_seconds) override;
    // Global brightness scaler 0..100.
    void set_brightness_percent(int brightness_percent) override;
    // Base color tint for sparks/rockets (default warm white if not set).
    void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override;

private:
    struct Rocket {
        float u = 0.0f;      // vertical coordinate along major axis
        float minor = 0.0f;  // horizontal/secondary coordinate
        float vu = 0.0f;     // vertical speed
        float vminor = 0.0f; // horizontal speed
        uint64_t start_us = 0;
        uint64_t last_us = 0;
        bool active = false;
    };

    struct Spark {
        float u = 0.0f;
        float minor = 0.0f;
        float vu = 0.0f;
        float vminor = 0.0f;
        float origin_u = 0.0f;
        float origin_minor = 0.0f;
        uint64_t start_us = 0;
        uint64_t last_us = 0;
        float life_s = 0.0f;
        uint8_t r = 255, g = 200, b = 120;
        uint8_t mode = 0; // 0=ring, 1=spokes, 2=solid
        bool active = true;
    };

    // Config/state
    int duration_seconds_ = 8;      // overall lifetime scale
    int brightness_percent_ = 100;  // global intensity
    uint8_t base_r_ = 255, base_g_ = 220, base_b_ = 160, base_w_ = 0;
    bool base_color_set_ = false;

    size_t major_len_ = 1;  // vertical axis length
    size_t minor_len_ = 1;  // secondary axis length (1 for 1D)
    size_t real_rows_ = 1;
    size_t real_cols_ = 1;

    Rocket rocket_;
    std::vector<Spark> sparks_;
    uint64_t last_launch_us_ = 0;
    uint8_t last_explosion_mode_ = 0; // for diagnostics if needed

    // Helpers
    void update_geometry(LEDStrip& strip);
    uint64_t lifetime_us() const;
    void ensure_rocket(uint64_t now_us);
    void spawn_rocket(uint64_t now_us);
    void explode_rocket(uint64_t now_us);
    void update_rocket(uint64_t now_us);
    void update_sparks(uint64_t now_us);
    void render(LEDStrip& strip);
};

} // namespace leds



