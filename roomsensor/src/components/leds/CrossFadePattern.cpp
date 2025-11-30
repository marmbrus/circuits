#include "CrossFadePattern.h"
#include "LEDStrip.h"

namespace leds {

uint64_t CrossFadePattern::phase_duration_us() const {
    int s = duration_seconds_;
    if (s <= 0) s = 1;
    return static_cast<uint64_t>(s) * 1'000'000ULL;
}

void CrossFadePattern::set_speed_percent(int speed_seconds) {
    if (speed_seconds < 0) speed_seconds = 0;
    duration_seconds_ = speed_seconds;
}

void CrossFadePattern::set_brightness_percent(int brightness_percent) {
    if (brightness_percent < 0) brightness_percent = 0;
    if (brightness_percent > 100) brightness_percent = 100;
    brightness_percent_ = brightness_percent;
}

void CrossFadePattern::set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    r_ = r; g_ = g; b_ = b; w_ = w;
    color_set_ = true;
}

void CrossFadePattern::reset(LEDStrip& strip, uint64_t now_us) {
    rows_ = strip.rows();
    cols_ = strip.cols();
    if (rows_ == 0) rows_ = 1;
    if (cols_ == 0) cols_ = strip.length();

    phase_ = Phase::ROW_DOWN;
    phase_start_us_ = now_us;
}

void CrossFadePattern::update(LEDStrip& strip, uint64_t now_us) {
    if (!color_set_) {
        // Default to white if user hasn't provided a color.
        r_ = g_ = b_ = 255; w_ = 0;
        color_set_ = true;
    }

    rows_ = strip.rows();
    cols_ = strip.cols();
    if (rows_ == 0) rows_ = 1;
    if (cols_ == 0) cols_ = strip.length();
    if (cols_ == 0) return;

    uint64_t dur_us = phase_duration_us();
    uint64_t elapsed = now_us - phase_start_us_;
    if (dur_us == 0) dur_us = 1;

    // Fraction of the current sweep that has completed (0..1]
    float frac = static_cast<float>(elapsed >= dur_us ? dur_us : elapsed) / static_cast<float>(dur_us);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    size_t total_len = strip.length();
    if (total_len == 0) return;

    // Compute brightness scaling (simple global scaler on color channels)
    float scale = static_cast<float>(brightness_percent_) / 100.0f;
    if (scale < 0.0f) scale = 0.0f;
    if (scale > 1.0f) scale = 1.0f;

    uint8_t sr = static_cast<uint8_t>(static_cast<float>(r_) * scale + 0.5f);
    uint8_t sg = static_cast<uint8_t>(static_cast<float>(g_) * scale + 0.5f);
    uint8_t sb = static_cast<uint8_t>(static_cast<float>(b_) * scale + 0.5f);
    uint8_t sw = static_cast<uint8_t>(static_cast<float>(w_) * scale + 0.5f);

    // Determine how many lines should be affected in this phase
    if (phase_ == Phase::ROW_DOWN) {
        // Start with all rows ON, turn them OFF one by one from top to bottom.
        size_t N = rows_;
        if (N == 0) N = 1;
        size_t lines_off = static_cast<size_t>(frac * static_cast<float>(N));
        if (lines_off > N) lines_off = N;

        for (size_t r = 0; r < rows_; ++r) {
            bool on = (r >= lines_off); // rows before lines_off are OFF
            for (size_t c = 0; c < cols_; ++c) {
                size_t idx = strip.index_for_row_col(r, c);
                if (idx >= total_len) continue;
                if (on) strip.set_pixel(idx, sr, sg, sb, sw);
                else    strip.set_pixel(idx, 0, 0, 0, 0);
            }
        }
    } else { // Phase::COL_UP
        // Start with all columns OFF, turn them ON one by one from left to right.
        size_t N = cols_;
        if (N == 0) N = 1;
        size_t lines_on = static_cast<size_t>(frac * static_cast<float>(N));
        if (lines_on > N) lines_on = N;

        for (size_t c = 0; c < cols_; ++c) {
            bool on = (c < lines_on); // columns before lines_on are ON
            for (size_t r = 0; r < rows_; ++r) {
                size_t idx = strip.index_for_row_col(r, c);
                if (idx >= total_len) continue;
                if (on) strip.set_pixel(idx, sr, sg, sb, sw);
                else    strip.set_pixel(idx, 0, 0, 0, 0);
            }
        }
    }

    // If this phase has finished, move to the next one.
    if (elapsed >= dur_us) {
        phase_start_us_ = now_us;
        phase_ = (phase_ == Phase::ROW_DOWN) ? Phase::COL_UP : Phase::ROW_DOWN;
    }
}

} // namespace leds




