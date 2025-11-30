#include "MeteorPattern.h"
#include "LEDStrip.h"
#include "esp_random.h"
#include "esp_log.h"
#include <algorithm>
#include <cmath>

namespace leds {

static const char* TAG_METEOR = "MeteorPattern";

uint64_t MeteorPattern::meteor_duration_us() const {
    int seconds = duration_seconds_;
    if (seconds <= 0) seconds = 1;
    return static_cast<uint64_t>(seconds) * 1'000'000ULL;
}

uint64_t MeteorPattern::spawn_interval_us() const {
    // Aim for ~target_active_meteors_ concurrent meteors.
    // Rough approximation: interval = duration / target_active_meteors_
    uint64_t dur = meteor_duration_us();
    if (target_active_meteors_ == 0) return dur;
    return dur / target_active_meteors_;
}

void MeteorPattern::set_speed_percent(int speed_seconds) {
    if (speed_seconds < 0) speed_seconds = 0;
    duration_seconds_ = speed_seconds;
}

void MeteorPattern::set_brightness_percent(int brightness_percent) {
    if (brightness_percent < 0) brightness_percent = 0;
    if (brightness_percent > 100) brightness_percent = 100;
    brightness_percent_ = brightness_percent;
}

void MeteorPattern::reset(LEDStrip& strip, uint64_t now_us) {
    strip_length_ = strip.length();
    meteors_.clear();
    last_spawn_us_ = now_us;

    ESP_LOGI(TAG_METEOR, "Reset: strip_length=%zu, duration=%d s, brightness=%d%%",
             strip_length_, duration_seconds_, brightness_percent_);
}

void MeteorPattern::spawn_meteor(uint64_t now_us) {
    if (strip_length_ == 0) return;
    Meteor m;
    m.start_us = now_us;
    // Choose a random center in [0, strip_length_ - 1]
    uint32_t r = esp_random();
    m.center = static_cast<float>(r % (strip_length_ == 0 ? 1 : strip_length_));
    meteors_.push_back(m);
    last_spawn_us_ = now_us;
}

void MeteorPattern::update(LEDStrip& strip, uint64_t now_us) {
    strip_length_ = strip.length();
    if (strip_length_ == 0) return;

    // If brightness is 0, just clear the strip and skip processing.
    if (brightness_percent_ <= 0) {
        for (size_t i = 0; i < strip_length_; ++i) {
            strip.set_pixel(i, 0, 0, 0, 0);
        }
        meteors_.clear();
        return;
    }

    uint64_t dur_us = meteor_duration_us();

    // Remove expired meteors
    meteors_.erase(
        std::remove_if(meteors_.begin(), meteors_.end(),
                       [now_us, dur_us](const Meteor& m) {
                           return (now_us - m.start_us) >= dur_us;
                       }),
        meteors_.end());

    // Maintain ~target_active_meteors_ with staggered starts
    if ((meteors_.size() < target_active_meteors_) &&
        (now_us - last_spawn_us_ >= spawn_interval_us())) {
        spawn_meteor(now_us);
    }

    // Compute brightness scale
    float brightness_scale = static_cast<float>(brightness_percent_) / 100.0f;

    // For each pixel, accumulate contribution from all active meteors
    for (size_t i = 0; i < strip_length_; ++i) {
        float best_amp = 0.0f;
        float pos = static_cast<float>(i);

        for (const auto& m : meteors_) {
            uint64_t elapsed_us = now_us - m.start_us;
            if (elapsed_us >= dur_us) continue;

            float p = static_cast<float>(elapsed_us) / static_cast<float>(dur_us); // 0..1
            if (p < 0.0f) p = 0.0f;
            if (p > 1.0f) p = 1.0f;

            // Meteor grows in radius and fades out over time.
            // Start as a single bright pixel, then expand and fade over ~duration.
            float max_radius = std::max(2.0f, static_cast<float>(strip_length_) * 0.2f); // up to 20% of strip
            float radius = max_radius * p;
            float dist = std::fabs(pos - m.center);
            if (dist > radius) continue;

            // Shell profile: strongest near the center, decays with distance.
            float spatial = 1.0f - (dist / (radius + 1.0f)); // 1 at center, ~0 at radius
            if (spatial < 0.0f) spatial = 0.0f;

            // Temporal fade: starts bright, fades to 0 by the end.
            float temporal = 1.0f - p;

            float amp = spatial * temporal;
            if (amp > best_amp) best_amp = amp;
        }

        if (best_amp <= 0.0f) {
            strip.set_pixel(i, 0, 0, 0, 0);
        } else {
            float v = best_amp * brightness_scale;
            if (v > 1.0f) v = 1.0f;
            uint8_t val = static_cast<uint8_t>(v * 255.0f + 0.5f);
            // Bright white meteor; favor RGB so it works on RGB strips.
            strip.set_pixel(i, val, val, val, 0);
        }
    }
}

} // namespace leds





