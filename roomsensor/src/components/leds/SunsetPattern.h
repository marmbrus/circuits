#pragma once

#include "LEDPattern.h"
#include <cstddef>
#include <vector>

namespace leds {

// Smooth, slowly undulating sunset of orange / red / pink bands.
// Multiple color lobes drift across the strip and blend where they meet.
class SunsetPattern final : public LEDPattern {
public:
    const char* name() const override { return "SUNSET"; }
    void reset(LEDStrip& strip, uint64_t now_us) override;
    void update(LEDStrip& strip, uint64_t now_us) override;

    // Interpret speed as motion speed of the lobes: 0 => very slow, 100 => quite fast.
    void set_speed_percent(int speed_percent) override;
    // Global intensity scaler (0..100).
    void set_brightness_percent(int brightness_percent) override;

private:
    struct Lobe {
        float base_center = 0.0f;  // nominal center in [0, length)
        float amplitude = 0.0f;    // movement amplitude in LEDs
        float phase = 0.0f;        // initial phase offset (radians)
        float speed = 0.0f;        // angular speed (radians per second)
        uint8_t r = 0, g = 0, b = 0;
    };

    // Parameters / state
    size_t strip_length_ = 0;
    uint64_t start_us_ = 0;
    int speed_percent_ = 30;       // 0..100
    int brightness_percent_ = 100; // 0..100

    std::vector<Lobe> lobes_;

    // Helpers
    void init_lobes(size_t length);
    float effective_time_seconds(uint64_t now_us) const;
};

} // namespace leds



