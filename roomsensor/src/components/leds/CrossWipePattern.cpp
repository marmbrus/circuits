#include "CrossWipePattern.h"
#include "LEDStrip.h"
#include <algorithm>

namespace leds {

uint64_t CrossWipePattern::phase_duration_us() const {
    int s = duration_seconds_;
    if (s <= 0) s = 1;
    return static_cast<uint64_t>(s) * 1'000'000ULL;
}

void CrossWipePattern::set_speed_percent(int speed_seconds) {
    if (speed_seconds < 0) speed_seconds = 0;
    duration_seconds_ = speed_seconds;
}

void CrossWipePattern::set_brightness_percent(int brightness_percent) {
    if (brightness_percent < 0) brightness_percent = 0;
    if (brightness_percent > 100) brightness_percent = 100;
    brightness_percent_ = brightness_percent;
}

void CrossWipePattern::set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    r_ = r; g_ = g; b_ = b; w_ = w;
    color_set_ = true;
}

void CrossWipePattern::reset(LEDStrip& strip, uint64_t now_us) {
    rows_ = strip.rows();
    cols_ = strip.cols();
    if (rows_ == 0) rows_ = 1;
    if (cols_ == 0) cols_ = strip.length();

    phase_ = Phase::ROW;
    phase_start_us_ = now_us;
}

void CrossWipePattern::update(LEDStrip& strip, uint64_t now_us) {
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

    // Determine normalized phase progress [0,1)
    float frac = static_cast<float>(elapsed % dur_us) / static_cast<float>(dur_us);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    // Compute active index for current phase
    size_t active_row = 0;
    size_t active_col = 0;

    if (phase_ == Phase::ROW) {
        if (rows_ <= 1) {
            // Degenerate: no rows; treat like column wipe instead
            phase_ = Phase::COL;
            phase_start_us_ = now_us;
        } else {
            size_t idx = static_cast<size_t>(frac * static_cast<float>(rows_));
            if (idx >= rows_) idx = rows_ - 1;
            active_row = idx;
        }
    }

    if (phase_ == Phase::COL) {
        size_t idx = static_cast<size_t>(frac * static_cast<float>(cols_));
        if (idx >= cols_) idx = cols_ - 1;
        active_col = idx;
    }

    // When we finish one full phase, toggle to the other on next tick
    if (elapsed >= dur_us) {
        phase_start_us_ = now_us;
        phase_ = (phase_ == Phase::ROW) ? Phase::COL : Phase::ROW;
    }

    // Compute brightness scaling (simple per-pixel scaler, not spatial duty here)
    float scale = static_cast<float>(brightness_percent_) / 100.0f;
    if (scale < 0.0f) scale = 0.0f;
    if (scale > 1.0f) scale = 1.0f;

    uint8_t sr = static_cast<uint8_t>(static_cast<float>(r_) * scale + 0.5f);
    uint8_t sg = static_cast<uint8_t>(static_cast<float>(g_) * scale + 0.5f);
    uint8_t sb = static_cast<uint8_t>(static_cast<float>(b_) * scale + 0.5f);
    uint8_t sw = static_cast<uint8_t>(static_cast<float>(w_) * scale + 0.5f);

    // Clear strip and draw the active row/column
    size_t total_len = strip.length();
    for (size_t i = 0; i < total_len; ++i) {
        strip.set_pixel(i, 0, 0, 0, 0);
    }

    const size_t thickness = 4;

    if (phase_ == Phase::ROW) {
        // Light a band of up to 4 adjacent rows starting at active_row.
        size_t row_end = std::min(rows_, active_row + thickness);
        for (size_t r = active_row; r < row_end; ++r) {
            for (size_t c = 0; c < cols_; ++c) {
                size_t idx = strip.index_for_row_col(r, c);
                if (idx < total_len) strip.set_pixel(idx, sr, sg, sb, sw);
            }
        }
    } else { // COL
        // Light a band of up to 4 adjacent columns starting at active_col.
        size_t col_end = std::min(cols_, active_col + thickness);
        for (size_t c = active_col; c < col_end; ++c) {
            for (size_t r = 0; r < rows_; ++r) {
                size_t idx = strip.index_for_row_col(r, c);
                if (idx < total_len) strip.set_pixel(idx, sr, sg, sb, sw);
            }
        }
    }
}

} // namespace leds



