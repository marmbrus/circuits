#include "FadePattern.h"
#include "LEDStrip.h"
#include <cmath>

namespace leds {

void FadePattern::update(LEDStrip& strip, uint64_t now_us) {
    if (!initialized_) { fade_start_us_ = now_us; initialized_ = true; }

    // If the target (color or brightness) changed since last update, capture a new segment start
    if (target_dirty_) {
        // Use the last color we actually output as the starting point for the new fade
        // instead of sampling a specific pixel, which may be OFF due to spatial duty.
        start_r_ = last_out_r_;
        start_g_ = last_out_g_;
        start_b_ = last_out_b_;
        start_w_ = last_out_w_;
        // Also fade brightness duty from the last effective level to the new target level
        start_brightness_percent_ = last_out_brightness_percent_;
        fade_start_us_ = now_us;
        target_dirty_ = false;
    }

    // Compute progress linearly in time
    float duration_s = (duration_seconds_ <= 0) ? 0.0f : static_cast<float>(duration_seconds_);
    float t = 1.0f;
    if (duration_s > 0.0f) {
        float elapsed_s = static_cast<float>(now_us - fade_start_us_) / 1'000'000.0f;
        if (elapsed_s < 0) elapsed_s = 0;
        t = (elapsed_s >= duration_s) ? 1.0f : (elapsed_s / duration_s);
    }

    // Smooth visually: apply gamma to blend factor to compensate perceived brightness (approx gamma 2.2)
    float tg = t;
    const float gamma = 2.2f;
    tg = powf(tg, 1.0f / gamma);

    auto lerp_u8 = [](uint8_t a, uint8_t b, float u) -> uint8_t {
        float af = static_cast<float>(a);
        float bf = static_cast<float>(b);
        float vf = af + (bf - af) * u;
        if (vf < 0.0f) {
            vf = 0.0f;
        }
        if (vf > 255.0f) {
            vf = 255.0f;
        }
        return static_cast<uint8_t>(vf + 0.5f);
    };

    uint8_t r = lerp_u8(start_r_, target_r_, tg);
    uint8_t g = lerp_u8(start_g_, target_g_, tg);
    uint8_t b = lerp_u8(start_b_, target_b_, tg);
    uint8_t w = lerp_u8(start_w_, target_w_, tg);

    // Fade brightness duty between start and target as well, matching SolidPattern spacing:
    // - brightness <= 0  : all OFF
    // - brightness >= 100: all pixels at (r,g,b,w)
    // - 0 < brightness < 100: exactly K LEDs ON, spaced as evenly as possible
    float b_lerp = static_cast<float>(start_brightness_percent_) +
                   (static_cast<float>(target_brightness_percent_) - static_cast<float>(start_brightness_percent_)) * tg;
    int bp = static_cast<int>(b_lerp + 0.5f);
    if (bp <= 0) {
        for (size_t i = 0; i < strip.length(); ++i) {
            strip.set_pixel(i, 0, 0, 0, 0);
        }
        last_out_r_ = 0; last_out_g_ = 0; last_out_b_ = 0; last_out_w_ = 0;
        last_out_brightness_percent_ = 0;
        return;
    }
    if (bp >= 100) {
        for (size_t i = 0; i < strip.length(); ++i) {
            strip.set_pixel(i, r, g, b, w);
        }
        last_out_r_ = r; last_out_g_ = g; last_out_b_ = b; last_out_w_ = w;
        last_out_brightness_percent_ = 100;
        return;
    }

    size_t total = strip.length();
    size_t on_count = (total * static_cast<size_t>(bp)) / 100u;
    if (on_count == 0) {
        for (size_t i = 0; i < total; ++i) strip.set_pixel(i, 0, 0, 0, 0);
        last_out_r_ = 0; last_out_g_ = 0; last_out_b_ = 0; last_out_w_ = 0;
        last_out_brightness_percent_ = 0;
        return;
    }
    if (on_count >= total) {
        for (size_t i = 0; i < total; ++i) strip.set_pixel(i, r, g, b, w);
        last_out_r_ = r; last_out_g_ = g; last_out_b_ = b; last_out_w_ = w;
        last_out_brightness_percent_ = 100;
        return;
    }

    size_t acc = 0;
    for (size_t i = 0; i < total; ++i) {
        acc += on_count;
        bool on = false;
        if (acc >= total) { on = true; acc -= total; }
        if (on) strip.set_pixel(i, r, g, b, w);
        else strip.set_pixel(i, 0, 0, 0, 0);
    }
    // last_out_* tracks the representative ON color
    last_out_r_ = r; last_out_g_ = g; last_out_b_ = b; last_out_w_ = w;
    last_out_brightness_percent_ = bp;
}

} // namespace leds


