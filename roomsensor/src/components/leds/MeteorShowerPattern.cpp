#include "MeteorShowerPattern.h"
#include "LEDStrip.h"
#include <math.h>
#include <algorithm>

namespace leds {

void MeteorShowerPattern::reset(LEDStrip& strip, uint64_t now_us) {
    start_us_ = now_us;
    last_spawn_us_ = now_us;
    meteors_.clear();
    meteors_.resize(MAX_METEORS);
    
    // Initialize random state based on current time
    random_state_ = static_cast<uint32_t>(now_us & 0xFFFFFFFF);
}

uint32_t MeteorShowerPattern::simple_random() {
    // Simple linear congruential generator
    random_state_ = random_state_ * 1103515245 + 12345;
    return random_state_;
}

void MeteorShowerPattern::spawn_meteor(size_t strip_length, uint64_t now_us) {
    if (strip_length == 0) return;
    
    // Find an inactive meteor slot
    Meteor* meteor = nullptr;
    for (auto& m : meteors_) {
        if (!m.active) {
            meteor = &m;
            break;
        }
    }
    
    if (!meteor) return; // No available slots
    
    // Configure the new meteor
    meteor->active = true;
    meteor->birth_time_us = now_us;
    meteor->death_time_us = now_us + METEOR_LIFETIME_US;
    
    // Start near center of strip
    float center = static_cast<float>(strip_length) * 0.5f;
    float start_offset = (static_cast<float>(simple_random() % 1000) / 1000.0f - 0.5f) * 0.3f * static_cast<float>(strip_length);
    meteor->position = center + start_offset;
    
    // Random direction away from center
    meteor->direction = (simple_random() % 2) ? 1.0f : -1.0f;
    
    // Random velocity based on speed setting
    float speed_multiplier = (speed_percent_ <= 0) ? 0.1f : (speed_percent_ / 100.0f);
    float base_vel = BASE_VELOCITY * speed_multiplier;
    float vel_variation = (static_cast<float>(simple_random() % 1000) / 1000.0f - 0.5f) * VELOCITY_VARIATION * speed_multiplier;
    meteor->velocity = base_vel + vel_variation;
    
    // Initialize trail
    meteor->trail_positions.clear();
    meteor->trail_brightness.clear();
    meteor->trail_positions.reserve(MAX_TRAIL_LENGTH);
    meteor->trail_brightness.reserve(MAX_TRAIL_LENGTH);
}

void MeteorShowerPattern::update_meteor(Meteor& meteor, size_t strip_length, uint64_t now_us, float dt_seconds) {
    if (!meteor.active) return;
    
    // Check if meteor should be removed
    if (now_us >= meteor.death_time_us) {
        meteor.active = false;
        return;
    }
    
    // Update position
    float old_position = meteor.position;
    meteor.position += meteor.direction * meteor.velocity * dt_seconds;
    
    // Add current position to trail (before checking bounds)
    meteor.trail_positions.push_back(old_position);
    meteor.trail_brightness.push_back(1.0f);
    
    // Limit trail length
    if (meteor.trail_positions.size() > MAX_TRAIL_LENGTH) {
        meteor.trail_positions.erase(meteor.trail_positions.begin());
        meteor.trail_brightness.erase(meteor.trail_brightness.begin());
    }
    
    // Update trail brightness (fade over time)
    for (size_t i = 0; i < meteor.trail_brightness.size(); ++i) {
        // Older trail segments (lower indices) fade faster
        float age_factor = static_cast<float>(i) / static_cast<float>(meteor.trail_brightness.size());
        float fade_rate = 3.0f; // Trails fade quickly
        meteor.trail_brightness[i] = fmaxf(0.0f, meteor.trail_brightness[i] - fade_rate * dt_seconds * (1.0f - age_factor * 0.5f));
    }
    
    // Remove completely faded trail segments
    while (!meteor.trail_positions.empty() && meteor.trail_brightness.front() <= 0.01f) {
        meteor.trail_positions.erase(meteor.trail_positions.begin());
        meteor.trail_brightness.erase(meteor.trail_brightness.begin());
    }
    
    // Deactivate meteor if it's moved off the strip and has no visible trail
    if ((meteor.position < -5.0f || meteor.position >= static_cast<float>(strip_length) + 5.0f) && 
        meteor.trail_positions.empty()) {
        meteor.active = false;
    }
}

float MeteorShowerPattern::get_meteor_brightness(const Meteor& meteor, uint64_t now_us) const {
    uint64_t age_us = now_us - meteor.birth_time_us;
    uint64_t remaining_us = meteor.death_time_us - now_us;
    
    // Fade in quickly at birth
    float fade_in = 1.0f;
    if (age_us < 100000) { // 0.1 second fade in
        fade_in = static_cast<float>(age_us) / 100000.0f;
    }
    
    // Fade out at end of life
    float fade_out = 1.0f;
    if (remaining_us < 500000) { // 0.5 second fade out
        fade_out = static_cast<float>(remaining_us) / 500000.0f;
    }
    
    return fade_in * fade_out;
}

float MeteorShowerPattern::get_trail_brightness(float base_brightness, size_t trail_index, size_t trail_length) const {
    if (trail_length == 0) return 0.0f;
    
    // Trail gets dimmer towards the back
    float position_factor = static_cast<float>(trail_index) / static_cast<float>(trail_length);
    return base_brightness * position_factor * position_factor; // Quadratic falloff
}

void MeteorShowerPattern::render_meteor(LEDStrip& strip, const Meteor& meteor, uint64_t now_us) {
    if (!meteor.active) return;
    
    float meteor_brightness = get_meteor_brightness(meteor, now_us);
    
    // Render the main meteor (bright white)
    int meteor_pixel = static_cast<int>(roundf(meteor.position));
    if (meteor_pixel >= 0 && meteor_pixel < static_cast<int>(strip.length())) {
        uint8_t brightness = static_cast<uint8_t>(255.0f * meteor_brightness * (brightness_percent_ / 100.0f));
        strip.set_pixel(static_cast<size_t>(meteor_pixel), brightness, brightness, brightness, 0);
    }
    
    // Render the trail
    for (size_t i = 0; i < meteor.trail_positions.size(); ++i) {
        float trail_pos = meteor.trail_positions[i];
        int trail_pixel = static_cast<int>(roundf(trail_pos));
        
        if (trail_pixel >= 0 && trail_pixel < static_cast<int>(strip.length()) && trail_pixel != meteor_pixel) {
            float trail_brightness = get_trail_brightness(meteor.trail_brightness[i], i, meteor.trail_positions.size());
            trail_brightness *= meteor_brightness; // Apply meteor's overall brightness
            
            uint8_t brightness = static_cast<uint8_t>(255.0f * trail_brightness * (brightness_percent_ / 100.0f));
            
            // Trail is dimmer and slightly blue-white
            uint8_t r = static_cast<uint8_t>(brightness * 0.9f);
            uint8_t g = static_cast<uint8_t>(brightness * 0.9f);
            uint8_t b = brightness;
            
            // Blend with existing pixel (additive)
            uint8_t existing_r, existing_g, existing_b, existing_w;
            strip.get_pixel(static_cast<size_t>(trail_pixel), existing_r, existing_g, existing_b, existing_w);
            
            r = static_cast<uint8_t>(fminf(255.0f, existing_r + r));
            g = static_cast<uint8_t>(fminf(255.0f, existing_g + g));
            b = static_cast<uint8_t>(fminf(255.0f, existing_b + b));
            
            strip.set_pixel(static_cast<size_t>(trail_pixel), r, g, b, 0);
        }
    }
}

void MeteorShowerPattern::update(LEDStrip& strip, uint64_t now_us) {
    size_t strip_length = strip.length();
    if (strip_length == 0) return;
    
    // Clear the strip
    strip.clear();
    
    // Calculate time delta
    static uint64_t last_update_us = now_us;
    float dt_seconds = static_cast<float>(now_us - last_update_us) / 1000000.0f;
    last_update_us = now_us;
    
    // Clamp dt to reasonable values
    dt_seconds = fmaxf(0.001f, fminf(0.1f, dt_seconds));
    
    // Determine spawn rate based on strip length and speed
    uint64_t spawn_interval = MIN_SPAWN_INTERVAL_US;
    if (strip_length > 50) {
        spawn_interval = MIN_SPAWN_INTERVAL_US * 2 / 3; // More frequent spawning for longer strips
    }
    if (strip_length > 100) {
        spawn_interval = MIN_SPAWN_INTERVAL_US / 2; // Even more frequent for very long strips
    }
    
    // Adjust spawn rate based on speed setting
    if (speed_percent_ > 50) {
        spawn_interval = static_cast<uint64_t>(spawn_interval * (150 - speed_percent_) / 100.0f);
    }
    
    // Spawn new meteors
    if (now_us - last_spawn_us_ >= spawn_interval) {
        spawn_meteor(strip_length, now_us);
        last_spawn_us_ = now_us;
    }
    
    // Update all meteors
    for (auto& meteor : meteors_) {
        update_meteor(meteor, strip_length, now_us, dt_seconds);
    }
    
    // Render all meteors
    for (const auto& meteor : meteors_) {
        render_meteor(strip, meteor, now_us);
    }
}

} // namespace leds
