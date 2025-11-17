#include "SunsetPattern.h"
#include "LEDStrip.h"
#include "esp_random.h"
#include <cmath>
#include <algorithm>

namespace leds {

void SunsetPattern::set_speed_percent(int speed_percent) {
    if (speed_percent < 0) speed_percent = 0;
    if (speed_percent > 100) speed_percent = 100;
    speed_percent_ = speed_percent;
}

void SunsetPattern::set_brightness_percent(int brightness_percent) {
    if (brightness_percent < 0) brightness_percent = 0;
    if (brightness_percent > 100) brightness_percent = 100;
    brightness_percent_ = brightness_percent;
}

void SunsetPattern::init_lobes(size_t length) {
    lobes_.clear();
    if (length == 0) return;

    // Define three primary sunset colors
    // Orange, deep red, pink
    const uint8_t colors[3][3] = {
        {255, 120, 0},   // orange
        {255, 40,  0},   // red
        {255, 90, 160},  // pink
    };

    float L = static_cast<float>(length);
    for (int i = 0; i < 3; ++i) {
        Lobe l;
        l.base_center = L * (static_cast<float>(i + 1) / 4.0f); // roughly 1/4, 2/4, 3/4
        l.amplitude = L * 0.25f;                               // move over ~50% of strip
        // randomize phase and speed a bit
        uint32_t r = esp_random();
        l.phase = static_cast<float>(r & 0xFFFF) / 65535.0f * 2.0f * static_cast<float>(M_PI);
        float base_speed = 0.03f; // rad/s baseline
        float jitter = (static_cast<float>((r >> 16) & 0xFF) / 255.0f - 0.5f) * 0.02f; // +/-0.01
        l.speed = base_speed + jitter;

        l.r = colors[i][0];
        l.g = colors[i][1];
        l.b = colors[i][2];
        lobes_.push_back(l);
    }
}

float SunsetPattern::effective_time_seconds(uint64_t now_us) const {
    float t = static_cast<float>(now_us - start_us_) / 1'000'000.0f;
    if (t < 0.0f) t = 0.0f;

    // Map speed_percent_ into a time scale; 0 => very slow, 100 => faster motion.
    // We treat this as a multiplier on base lobe speeds.
    float speed_scale = 0.2f + (static_cast<float>(speed_percent_) / 100.0f) * 1.8f; // 0.2x .. 2.0x
    return t * speed_scale;
}

void SunsetPattern::reset(LEDStrip& strip, uint64_t now_us) {
    strip_length_ = strip.length();
    start_us_ = now_us;
    init_lobes(strip_length_);
}

void SunsetPattern::update(LEDStrip& strip, uint64_t now_us) {
    strip_length_ = strip.length();
    if (strip_length_ == 0 || lobes_.empty()) return;

    float t = effective_time_seconds(now_us);
    float L = static_cast<float>(strip_length_);

    // Global "breathing" brightness modulation for extra motion
    float breathe = 0.75f + 0.25f * std::sin(t * 0.25f); // very slow 0.25 Hz-ish
    float global_scale = (static_cast<float>(brightness_percent_) / 100.0f) * breathe;

    for (size_t i = 0; i < strip_length_; ++i) {
        float pos = static_cast<float>(i);

        float R = 0.0f, G = 0.0f, B = 0.0f;

        for (const auto& l : lobes_) {
            // Compute moving center for this lobe
            float center = l.base_center + l.amplitude * std::sin(l.speed * t + l.phase);
            // Wrap center into [0, L)
            if (center < 0.0f) center += L;
            if (center >= L) center -= L;

            float dist = std::fabs(pos - center);
            // Take shortest distance on ring to avoid hard edges near endpoints
            if (dist > L * 0.5f) dist = L - dist;

            float sigma = L * 0.25f; // wide, soft lobes (~1/4 strip)
            if (sigma <= 0.0f) continue;

            float x = dist / sigma;
            // Gaussian-ish falloff
            float weight = std::exp(-0.5f * x * x);

            R += static_cast<float>(l.r) * weight;
            G += static_cast<float>(l.g) * weight;
            B += static_cast<float>(l.b) * weight;
        }

        // Normalize if too bright
        float maxC = std::max({R, G, B, 1.0f});
        if (maxC > 255.0f) {
            float inv = 255.0f / maxC;
            R *= inv; G *= inv; B *= inv;
        }

        // Apply global_scale to modulate intensity
        R *= global_scale;
        G *= global_scale;
        B *= global_scale;

        if (R < 0.0f) R = 0.0f;
        if (G < 0.0f) G = 0.0f;
        if (B < 0.0f) B = 0.0f;
        if (R > 255.0f) R = 255.0f;
        if (G > 255.0f) G = 255.0f;
        if (B > 255.0f) B = 255.0f;

        uint8_t r8 = static_cast<uint8_t>(R + 0.5f);
        uint8_t g8 = static_cast<uint8_t>(G + 0.5f);
        uint8_t b8 = static_cast<uint8_t>(B + 0.5f);

        strip.set_pixel(i, r8, g8, b8, 0);
    }
}

} // namespace leds



