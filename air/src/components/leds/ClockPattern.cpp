#include "ClockPattern.h"
#include "LEDStrip.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "font6x6.h"
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>

namespace leds {

static const char* TAG_CLOCK = "ClockPattern";
static constexpr bool kClockDrawOutline = true; // set false to disable outline progress
// 7-segment-style masks for digits 0-9: bits A..G (LSB=segment A)
// Segments: A=top, B=upper-right, C=lower-right, D=bottom, E=lower-left, F=upper-left, G=middle
// Common 7-seg (A..G) truth table. Bit order: A=1<<0 ... G=1<<6.
static const uint8_t DIGIT_MASKS[10] = {
    /*0*/ (1<<0)|(1<<1)|(1<<2)|(1<<3)|(1<<4)|(1<<5),           // A B C D E F
    /*1*/ (1<<1)|(1<<2),                                       // B C
    /*2*/ (1<<0)|(1<<1)|(1<<3)|(1<<4)|(1<<6),                  // A B D E G
    /*3*/ (1<<0)|(1<<1)|(1<<2)|(1<<3)|(1<<6),                  // A B C D G
    /*4*/ (1<<1)|(1<<2)|(1<<5)|(1<<6),                         // B C F G
    /*5*/ (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<6),                  // A C D F G
    /*6*/ (1<<0)|(1<<2)|(1<<3)|(1<<4)|(1<<5)|(1<<6),           // A C D E F G
    /*7*/ (1<<0)|(1<<1)|(1<<2),                                // A B C
    /*8*/ (1<<0)|(1<<1)|(1<<2)|(1<<3)|(1<<4)|(1<<5)|(1<<6),    // A B C D E F G
    /*9*/ (1<<0)|(1<<1)|(1<<2)|(1<<3)|(1<<5)|(1<<6),           // A B C D F G
};

static inline void put_pixel(LEDStrip& strip, size_t row, size_t col, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (row >= strip.rows() || col >= strip.cols()) return;
    size_t idx = strip.index_for_row_col(row, col);
    strip.set_pixel(idx, r, g, b, w);
}

// Draw thin horizontal segment in the 8x8 box at local row rr, spanning local columns c1..c2
static inline void draw_h(LEDStrip& strip, size_t top_row, size_t left_col, int rr, int c1, int c2,
                          uint8_t cr, uint8_t cg, uint8_t cb, uint8_t cw) {
    for (int c = c1; c <= c2; ++c) put_pixel(strip, top_row + static_cast<size_t>(rr), left_col + static_cast<size_t>(c), cr, cg, cb, cw);
}

// Draw thin vertical segment in the 8x8 box at local col cc, spanning local rows r1..r2
static inline void draw_v(LEDStrip& strip, size_t top_row, size_t left_col, int cc, int r1, int r2,
                          uint8_t cr, uint8_t cg, uint8_t cb, uint8_t cw) {
    for (int rr = r1; rr <= r2; ++rr) put_pixel(strip, top_row + static_cast<size_t>(rr), left_col + static_cast<size_t>(cc), cr, cg, cb, cw);
}

void ClockPattern::draw_digit(LEDStrip& strip, size_t top_row, size_t left_col, int digit,
                              uint8_t rr, uint8_t gg, uint8_t bb, uint8_t ww) {
    // Deprecated internal 7-seg; delegate to font6x6 for exact glyph shapes
    font6x6::draw_digit(strip, digit, top_row, left_col, rr, gg, bb, ww);
}

static inline void split_hh_mm(int& hh, int& mm) {
    struct timeval tv{};
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec <= 0) {
        // Fallback to uptime if RTC not set
        uint64_t us = esp_timer_get_time();
        uint64_t sec = us / 1000000ULL;
        uint32_t day_sec = static_cast<uint32_t>(sec % (24u * 3600u));
        hh = static_cast<int>(day_sec / 3600u);
        mm = static_cast<int>((day_sec % 3600u) / 60u);
        return;
    }
    time_t now = tv.tv_sec;
    struct tm lt;
    localtime_r(&now, &lt);
    hh = lt.tm_hour;
    mm = lt.tm_min;
}

void ClockPattern::render(LEDStrip& strip) {
    // Clear
    for (size_t i = 0; i < strip.length(); ++i) strip.set_pixel(i, 0, 0, 0, 0);

    // Layout: 16x16 assumption.
    // Top: HH with digits starting at columns 0 and 8.
    // Bottom: MM at top_row 8, columns 0 and 8.
    int hh, mm; split_hh_mm(hh, mm);
    // Convert to 12-hour time (1..12), keep leading zero rendering for HH
    int hh12 = ((hh + 11) % 12) + 1;
    int h1 = (hh12 / 10) % 10;
    int h2 = hh12 % 10;
    int m1 = (mm / 10) % 10;
    int m2 = mm % 10;

    auto scale = [this](uint8_t c) -> uint8_t { return static_cast<uint8_t>((static_cast<int>(c) * brightness_percent_) / 100); };
    uint8_t rr = scale(r_), gg = scale(g_), bb = scale(b_), ww = scale(w_);
    font6x6::draw_digit(strip, h1, 0, 0, rr, gg, bb, ww);
    font6x6::draw_digit(strip, h2, 0, 8, rr, gg, bb, ww);
    font6x6::draw_digit(strip, m1, 8, 0, rr, gg, bb, ww);
    font6x6::draw_digit(strip, m2, 8, 8, rr, gg, bb, ww);
    ESP_LOGD(TAG_CLOCK, "CLOCK render: %02d:%02d (H1=%d H2=%d M1=%d M2=%d) masks: H1=0x%02X H2=0x%02X M1=0x%02X M2=0x%02X",
             hh, mm, h1, h2, m1, m2, DIGIT_MASKS[h1], DIGIT_MASKS[h2], DIGIT_MASKS[m1], DIGIT_MASKS[m2]);

    if (kClockDrawOutline) draw_outline(strip);
}

void ClockPattern::update(LEDStrip& strip, uint64_t now_us) {
    (void)now_us;
    int hh, mm; split_hh_mm(hh, mm);
    int key = hh * 60 + mm;
    if (last_rendered_min_ == -1) {
        last_rendered_min_ = key;
        render(strip);
        return;
    }
    // Always update outline each tick to show seconds progress; redraw digits only when minute changes or knobs changed
    if (kClockDrawOutline) draw_outline(strip);
    if (needs_render_ || key != last_rendered_min_) {
        last_rendered_min_ = key;
        needs_render_ = false;
        render(strip);
    }
}

void ClockPattern::draw_outline(LEDStrip& strip) {
    if (!kClockDrawOutline) return;
    struct timeval tv{}; gettimeofday(&tv, nullptr);
    time_t now = tv.tv_sec; struct tm lt; localtime_r(&now, &lt);
    float sec_in_min = static_cast<float>(lt.tm_sec) + static_cast<float>(tv.tv_usec) / 1000000.0f;
    float frac = sec_in_min / 60.0f;
    // Head position goes around the perimeter (same 60-step mapping); snake length limited to kSnakeLen
    int head_seg = static_cast<int>(frac * 60.0f + 0.5f) % 60;
    auto scale = [this](uint8_t c) -> uint8_t { return static_cast<uint8_t>((static_cast<int>(c) * brightness_percent_) / 100); };
    uint8_t rr = scale(r_), gg = scale(g_), bb = scale(b_), ww = scale(w_);

    auto to_rc = [&](int seg, size_t& r, size_t& c) {
        size_t rows = strip.rows(); size_t cols = strip.cols();
        if (rows == 0 || cols == 0) { r = c = 0; return; }
        // Map 60 segments clockwise around the border starting at top-left moving right
        // top edge: 0..cols-1
        if (seg < static_cast<int>(cols)) { r = 0; c = static_cast<size_t>(seg); return; }
        seg -= static_cast<int>(cols);
        // right edge: 1..rows-1
        if (seg < static_cast<int>(rows) - 1) { r = static_cast<size_t>(seg + 1); c = cols - 1; return; }
        seg -= static_cast<int>(rows) - 1;
        // bottom edge: 1..cols-1 (right to left)
        if (seg < static_cast<int>(cols) - 1) { r = rows - 1; c = cols - 1 - static_cast<size_t>(seg + 1); return; }
        seg -= static_cast<int>(cols) - 1;
        // left edge: 1..rows-2 (bottom to top)
        size_t left_len = (rows >= 2) ? (rows - 2) : 0;
        if (seg < static_cast<int>(left_len)) { r = rows - 1 - static_cast<size_t>(seg + 1); c = 0; return; }
        r = 0; c = 0;
    };

    // Erase previous snake (
    for (int i = 0; i < last_snake_count_; ++i) {
        size_t idx = last_snake_idx_[static_cast<size_t>(i)];
        uint8_t zr=0,zg=0,zb=0,zw=0; strip.set_pixel(idx, zr, zg, zb, zw);
    }

    // Draw new snake head and trailing tail of length <= kSnakeLen
    int count = 0;
    for (int i = 0; i < kSnakeLen; ++i) {
        int seg = head_seg - i; if (seg < 0) seg += 60;
        size_t r, c; to_rc(seg, r, c);
        if (r >= strip.rows() || c >= strip.cols()) continue;
        size_t idx = strip.index_for_row_col(r, c);
        // Fade tail brightness linearly
        uint8_t fr = static_cast<uint8_t>((rr * (kSnakeLen - i)) / kSnakeLen);
        uint8_t fg = static_cast<uint8_t>((gg * (kSnakeLen - i)) / kSnakeLen);
        uint8_t fb = static_cast<uint8_t>((bb * (kSnakeLen - i)) / kSnakeLen);
        uint8_t fw = static_cast<uint8_t>((ww * (kSnakeLen - i)) / kSnakeLen);
        strip.set_pixel(idx, fr, fg, fb, fw);
        if (count < kSnakeLen) { last_snake_idx_[static_cast<size_t>(count)] = idx; }
        count++;
    }
    last_snake_count_ = count;
}

} // namespace leds



