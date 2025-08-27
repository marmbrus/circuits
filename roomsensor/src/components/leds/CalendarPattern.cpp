#include "CalendarPattern.h"
#include "LEDStrip.h"
#include "font6x6.h"
#include "esp_timer.h"
#include <time.h>

namespace leds {

static constexpr bool kCalendarDrawOutline = false; // set false to disable day outline
static inline void get_mm_dd(int& mm, int& dd) {
    time_t now = time(nullptr);
    if (now <= 0) {
        // Fallback: derive from uptime to have a changing key
        uint64_t us = esp_timer_get_time();
        uint64_t days = (us / 1000000ULL) / 86400ULL;
        mm = static_cast<int>((days % 12ull) + 1ull);
        dd = static_cast<int>((days % 28ull) + 1ull);
        return;
    }
    struct tm lt; localtime_r(&now, &lt);
    mm = lt.tm_mon + 1; // 1..12
    dd = lt.tm_mday;    // 1..31
}

static inline uint8_t scale(uint8_t c, int percent) { return static_cast<uint8_t>((static_cast<int>(c) * percent) / 100); }

void CalendarPattern::render(LEDStrip& strip) {
    for (size_t i = 0; i < strip.length(); ++i) strip.set_pixel(i, 0, 0, 0, 0);

    int mm, dd; get_mm_dd(mm, dd);
    char line1[3]; char line2[3];
    line1[0] = static_cast<char>('0' + (mm / 10) % 10);
    line1[1] = static_cast<char>('0' + (mm % 10));
    line1[2] = '\0';
    line2[0] = static_cast<char>('0' + (dd / 10) % 10);
    line2[1] = static_cast<char>('0' + (dd % 10));
    line2[2] = '\0';

    uint8_t rr = scale(r_, brightness_percent_);
    uint8_t gg = scale(g_, brightness_percent_);
    uint8_t bb = scale(b_, brightness_percent_);
    uint8_t ww = scale(w_, brightness_percent_);

    font6x6::draw_text(strip, line1, 0, 0, rr, gg, bb, ww);
    font6x6::draw_text(strip, line2, 8, 0, rr, gg, bb, ww);

    // Day progress around perimeter (0..60 segments across 24h)
    time_t now = time(nullptr);
    if (kCalendarDrawOutline && now > 0) {
        struct tm lt; localtime_r(&now, &lt);
        int seconds_today = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
        float frac = static_cast<float>(seconds_today) / (24.0f * 3600.0f);
        int segments = static_cast<int>(frac * 60.0f + 0.5f);
        auto draw_seg = [&](size_t r, size_t c) { if (r < strip.rows() && c < strip.cols()) strip.set_pixel(strip.index_for_row_col(r,c), rr, gg, bb, ww); };
        int drawn = 0;
        // top edge
        for (size_t x = 0; drawn < segments && x < strip.cols(); ++x, ++drawn) draw_seg(0, x);
        // right edge
        for (size_t y = 1; drawn < segments && y < strip.rows(); ++y, ++drawn) draw_seg(y, strip.cols() - 1);
        // bottom edge
        for (size_t xi = 1; drawn < segments && xi < strip.cols(); ++xi, ++drawn) draw_seg(strip.rows() - 1, strip.cols() - 1 - xi);
        // left edge
        for (size_t yi = 1; drawn < segments && yi < strip.rows() - 1; ++yi, ++drawn) draw_seg(strip.rows() - 1 - yi, 0);
    }
}

void CalendarPattern::update(LEDStrip& strip, uint64_t now_us) {
    (void)now_us;
    int mm, dd; get_mm_dd(mm, dd);
    int key = mm * 100 + dd;
    if (last_key_ == -1 || needs_render_ || key != last_key_) {
        last_key_ = key; needs_render_ = false; render(strip);
    }
}

} // namespace leds


