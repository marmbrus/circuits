#pragma once

#include "LEDPattern.h"
#include <cstddef>
#include <vector>

namespace leds {

class MeteorShowerPattern final : public LEDPattern {
public:
    const char* name() const override { return "METEOR_SHOWER"; }
    void reset(LEDStrip& strip, uint64_t now_us) override;
    void update(LEDStrip& strip, uint64_t now_us) override;
    void set_speed_percent(int speed_percent) override { speed_percent_ = speed_percent; }
    void set_brightness_percent(int brightness_percent) override {
        if (brightness_percent < 0) brightness_percent = 0;
        if (brightness_percent > 100) brightness_percent = 100;
        brightness_percent_ = brightness_percent;
    }

private:
    struct Meteor {
        float position;           // Current position along the strip (0.0 to strip_length)
        float velocity;          // Speed of movement (pixels per second)
        float direction;         // Direction: +1 for forward, -1 for backward
        uint64_t birth_time_us;  // When this meteor was created
        uint64_t death_time_us;  // When this meteor should be removed
        bool active;             // Whether this meteor is currently active
        
        // Trail management
        std::vector<float> trail_positions;  // Positions of trail segments
        std::vector<float> trail_brightness; // Brightness of each trail segment
        
        Meteor() : position(0), velocity(0), direction(1), birth_time_us(0), 
                   death_time_us(0), active(false) {}
    };
    
    uint64_t start_us_ = 0;
    int speed_percent_ = 50;
    int brightness_percent_ = 100;
    
    std::vector<Meteor> meteors_;
    uint64_t last_spawn_us_ = 0;
    
    // Configuration
    static constexpr size_t MAX_METEORS = 8;           // Maximum simultaneous meteors
    static constexpr float BASE_VELOCITY = 15.0f;     // Base pixels per second
    static constexpr float VELOCITY_VARIATION = 10.0f; // Velocity randomization range
    static constexpr uint64_t METEOR_LIFETIME_US = 3000000; // 3 seconds
    static constexpr uint64_t TRAIL_FADE_US = 800000;  // 0.8 seconds for trail to fade
    static constexpr size_t MAX_TRAIL_LENGTH = 12;     // Maximum trail segments
    static constexpr uint64_t MIN_SPAWN_INTERVAL_US = 200000; // 0.2 seconds minimum between spawns
    
    // Helper functions
    void spawn_meteor(size_t strip_length, uint64_t now_us);
    void update_meteor(Meteor& meteor, size_t strip_length, uint64_t now_us, float dt_seconds);
    void render_meteor(LEDStrip& strip, const Meteor& meteor, uint64_t now_us);
    float get_meteor_brightness(const Meteor& meteor, uint64_t now_us) const;
    float get_trail_brightness(float base_brightness, size_t trail_index, size_t trail_length) const;
    uint32_t simple_random();
    
    // Simple PRNG state
    mutable uint32_t random_state_ = 12345;
};

} // namespace leds
