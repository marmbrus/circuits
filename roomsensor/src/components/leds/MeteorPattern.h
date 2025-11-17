#pragma once

#include "LEDPattern.h"
#include <cstddef>
#include <vector>

namespace leds {

// Random "meteor" hits that appear as bright white flashes which expand and fade
// over time. Roughly ~5 meteors are active concurrently with staggered start times.
class MeteorPattern final : public LEDPattern {
public:
    const char* name() const override { return "METEOR"; }
    void reset(LEDStrip& strip, uint64_t now_us) override;
    void update(LEDStrip& strip, uint64_t now_us) override;

    // Interpret speed as duration in seconds for a single meteor: 0 => 1s, 10 => 10s, etc.
    void set_speed_percent(int speed_seconds) override;
    // Brightness scales meteor intensity (0..100).
    void set_brightness_percent(int brightness_percent) override;

private:
    struct Meteor {
        uint64_t start_us = 0;  // when this meteor started
        float center = 0.0f;    // center position in LED index space
    };

    // Parameters
    int duration_seconds_ = 10;   // lifetime of a meteor in seconds
    int brightness_percent_ = 100;
    size_t target_active_meteors_ = 5;

    // State
    size_t strip_length_ = 0;
    uint64_t last_spawn_us_ = 0;
    std::vector<Meteor> meteors_;

    // Helpers
    uint64_t meteor_duration_us() const;
    uint64_t spawn_interval_us() const;
    void spawn_meteor(uint64_t now_us);
};

} // namespace leds



