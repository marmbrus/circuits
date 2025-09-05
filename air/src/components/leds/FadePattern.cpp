#include "FadePattern.h"
#include "LEDStrip.h"
#include <cmath>

namespace leds {

void FadePattern::update(LEDStrip& strip, uint64_t now_us) {
    if (!initialized_) { fade_start_us_ = now_us; initialized_ = true; }

    // If the target changed since last update, capture a new segment start from current output
    if (target_dirty_) {
        // Attempt to read the current value from strip (best-effort). If unavailable, fall back to last_out_*
        // LEDStrip exposes get_pixel by index; we sample the first pixel as representative for global solid fade.
        uint8_t cr=last_out_r_, cg=last_out_g_, cb=last_out_b_, cw=last_out_w_;
        if (strip.length() > 0) {
            (void)strip.get_pixel(0, cr, cg, cb, cw);
        }
        start_r_ = cr; start_g_ = cg; start_b_ = cb; start_w_ = cw;
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

    for (size_t i = 0; i < strip.length(); ++i) {
        strip.set_pixel(i, r, g, b, w);
    }
    last_out_r_ = r; last_out_g_ = g; last_out_b_ = b; last_out_w_ = w;
}

} // namespace leds


