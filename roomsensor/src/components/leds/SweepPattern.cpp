#include "SweepPattern.h"
#include "LEDStrip.h"
#include "esp_log.h"

namespace leds {

static const char* TAG = "SweepPattern";

void SweepPattern::reset(LEDStrip& strip, uint64_t now_us) {
    strip_length_ = strip.length();
    last_applied_count_ = strip_length_; // assume fully in base color
    sweep_start_us_ = now_us;
    total_sweep_time_us_ = 0;
    
    ESP_LOGI(TAG, "Reset: strip_length=%zu, target=(%d,%d,%d,%d), brightness=%d%%", 
             strip_length_, target_r_, target_g_, target_b_, target_w_, target_brightness_percent_);
    
    // Initialize base/target bookkeeping. On first change, we will sweep from the
    // current base (usually off) to the requested target.
    base_r_ = 0;
    base_g_ = 0;
    base_b_ = 0;
    base_w_ = 0;
    base_brightness_percent_ = 0;

    last_target_r_ = target_r_;
    last_target_g_ = target_g_;
    last_target_b_ = target_b_;
    last_target_w_ = target_w_;
    last_target_brightness_percent_ = target_brightness_percent_;

    sweeping_ = false;
}

void SweepPattern::set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    target_r_ = r;
    target_g_ = g;
    target_b_ = b;
    target_w_ = w;
}

void SweepPattern::set_brightness_percent(int brightness_percent) {
    if (brightness_percent < 0) brightness_percent = 0;
    if (brightness_percent > 100) brightness_percent = 100;
    target_brightness_percent_ = brightness_percent;
}

void SweepPattern::set_speed_percent(int speed_percent) {
    if (speed_percent < 0) speed_percent = 0;
    if (speed_percent > 100) speed_percent = 100;
    speed_percent_ = speed_percent;
}

bool SweepPattern::has_changed() const {
    return target_r_ != last_target_r_ ||
           target_g_ != last_target_g_ ||
           target_b_ != last_target_b_ ||
           target_w_ != last_target_w_ ||
           target_brightness_percent_ != last_target_brightness_percent_;
}

// Apply brightness scaling to arbitrary color and brightness level
static inline void apply_brightness(uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& w, int brightness_percent) {
    if (brightness_percent <= 0) {
        r = g = b = w = 0;
        return;
    }
    if (brightness_percent >= 100) {
        return; // No scaling needed
    }
    
    // Scale color channels by brightness
    r = (r * static_cast<uint32_t>(brightness_percent)) / 100u;
    g = (g * static_cast<uint32_t>(brightness_percent)) / 100u;
    b = (b * static_cast<uint32_t>(brightness_percent)) / 100u;
    w = (w * static_cast<uint32_t>(brightness_percent)) / 100u;
}

void SweepPattern::apply_color(uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& w) const {
    apply_brightness(r, g, b, w, target_brightness_percent_);
}

void SweepPattern::update(LEDStrip& strip, uint64_t now_us) {
    // Check if strip length changed
    if (strip.length() != strip_length_) {
        strip_length_ = strip.length();
        if (last_applied_count_ > strip_length_) last_applied_count_ = strip_length_;
        // Keep sweeping flag; we'll recompute positions based on time below.
    }
    
    if (strip_length_ == 0) return;
    
    // Detect if color or brightness has changed (new target requested)
    if (has_changed()) {
        ESP_LOGI(TAG, "Change detected: new=(%d,%d,%d,%d) brightness=%d%%, last_target=(%d,%d,%d,%d) brightness=%d%%",
                 target_r_, target_g_, target_b_, target_w_, target_brightness_percent_,
                 last_target_r_, last_target_g_, last_target_b_, last_target_w_, last_target_brightness_percent_);

        // Start sweeping from the current base color to the new target.
        // Assume the strip is currently in base color for unswept pixels.
        // If no previous sweep completed, base_* will be "off".
        sweep_start_us_ = now_us;
        last_applied_count_ = 0;

        // Precompute sweep duration. "speed" directly encodes seconds.
        int seconds = (speed_percent_ <= 0) ? 1 : speed_percent_;
        total_sweep_time_us_ = static_cast<uint64_t>(seconds) * 1'000'000ULL;

        // Update last_target_* snapshot so further calls don't re-trigger.
        last_target_r_ = target_r_;
        last_target_g_ = target_g_;
        last_target_b_ = target_b_;
        last_target_w_ = target_w_;
        last_target_brightness_percent_ = target_brightness_percent_;

        sweeping_ = true;
    }
    
    // If not sweeping, nothing to do
    if (!sweeping_) {
        return;
    }
    
    // Compute how far the sweep has progressed *based on elapsed time*.
    if (total_sweep_time_us_ == 0) {
        // Safety: if duration not set, treat as instant update
        total_sweep_time_us_ = static_cast<uint64_t>((speed_percent_ <= 0 ? 1 : speed_percent_)) * 1'000'000ULL;
    }

    uint64_t elapsed = now_us - sweep_start_us_;
    double frac = (elapsed >= total_sweep_time_us_)
                      ? 1.0
                      : static_cast<double>(elapsed) / static_cast<double>(total_sweep_time_us_);
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;

    size_t sweep_pos = static_cast<size_t>(static_cast<double>(strip_length_) * frac);
    if (sweep_pos > strip_length_) sweep_pos = strip_length_;
    last_applied_count_ = sweep_pos;

    // Brightness semantics matching SolidPattern:
    // - brightness <= 0  : all pixels OFF
    // - brightness >= 100: all pixels ON (full color)
    // - 0 < brightness < 100: exactly K LEDs ON, spaced as evenly as possible
    auto clamp_brightness = [](int b) {
        if (b < 0) return 0;
        if (b > 100) return 100;
        return b;
    };

    int b_prev = clamp_brightness(base_brightness_percent_);
    int b_new = clamp_brightness(target_brightness_percent_);
    size_t total = strip_length_;

    size_t on_prev = (total * static_cast<size_t>(b_prev)) / 100u;
    size_t on_new = (total * static_cast<size_t>(b_new)) / 100u;

    size_t acc_prev = 0;
    size_t acc_new = 0;

    for (size_t i = 0; i < total; ++i) {
        bool prev_on = false;
        bool new_on = false;

        // Old pattern duty
        if (b_prev <= 0) {
            prev_on = false;
        } else if (b_prev >= 100) {
            prev_on = true;
        } else {
            acc_prev += on_prev;
            if (acc_prev >= total) {
                prev_on = true;
                acc_prev -= total;
            }
        }

        // New pattern duty
        if (b_new <= 0) {
            new_on = false;
        } else if (b_new >= 100) {
            new_on = true;
        } else {
            acc_new += on_new;
            if (acc_new >= total) {
                new_on = true;
                acc_new -= total;
            }
        }

        if (i < sweep_pos) {
            // This pixel has been swept: adopt new brightness duty and color
            if (new_on) {
                strip.set_pixel(i, target_r_, target_g_, target_b_, target_w_);
            } else {
                strip.set_pixel(i, 0, 0, 0, 0);
            }
        } else {
            // Not yet swept: keep old brightness duty and base color
            if (prev_on) {
                strip.set_pixel(i, base_r_, base_g_, base_b_, base_w_);
            } else {
                strip.set_pixel(i, 0, 0, 0, 0);
            }
        }
    }

    if (sweep_pos >= strip_length_) {
        ESP_LOGI(TAG, "Sweep complete (brightness=%d%%) across %zu pixels",
                 target_brightness_percent_, strip_length_);

        // After sweep completes, the new pattern becomes the baseline.
        base_r_ = target_r_;
        base_g_ = target_g_;
        base_b_ = target_b_;
        base_w_ = target_w_;
        base_brightness_percent_ = target_brightness_percent_;

        sweeping_ = false;
    }
}

} // namespace leds


