#include "SummaryPattern.h"
#include "LEDStrip.h"
#include "font6x6.h"
#include "esp_timer.h"
#include <time.h>
#include <cstring>

namespace leds {

static inline uint8_t scale(uint8_t c, int percent) { return static_cast<uint8_t>((static_cast<int>(c) * percent) / 100); }

static inline const char* day_suffix(int d) {
    int mod100 = d % 100;
    if (mod100 >= 11 && mod100 <= 13) return "th";
    switch (d % 10) {
        case 1: return "st";
        case 2: return "nd";
        case 3: return "rd";
        default: return "th";
    }
}

static inline void get_date_fields(int& mm, int& dd, int& wday) {
    time_t now = time(nullptr);
    if (now <= 0) {
        // Fallback: derive a changing but deterministic value from uptime
        uint64_t us = esp_timer_get_time();
        uint64_t days = (us / 1000000ULL) / 86400ULL;
        mm = static_cast<int>((days % 12ull) + 1ull);         // 1..12
        dd = static_cast<int>((days % 28ull) + 1ull);         // 1..28
        wday = static_cast<int>(days % 7ull);                 // 0..6 (Sun..Sat)
        return;
    }
    struct tm lt; localtime_r(&now, &lt);
    mm = lt.tm_mon + 1;           // 1..12
    dd = lt.tm_mday;              // 1..31
    wday = lt.tm_wday;            // 0..6 (Sun..Sat)
}

void SummaryPattern::render(LEDStrip& strip) {
    // Clear
    for (size_t i = 0; i < strip.length(); ++i) strip.set_pixel(i, 0, 0, 0, 0);

    int mm, dd, wday; get_date_fields(mm, dd, wday);

    static const char* MONTHS_ABBR[12] = {
        "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"
    };
    static const char* WEEKDAYS_ABBR[7] = {
        "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
    };
    const char* mon = MONTHS_ABBR[(mm >= 1 && mm <= 12) ? (mm - 1) : 0];
    const char* wdn = WEEKDAYS_ABBR[(wday >= 0 && wday < 7) ? wday : 0];
    char line1[16]; // e.g., "Mon"
    snprintf(line1, sizeof(line1), "%s", wdn);
    char line2[16]; // e.g., "Nov"
    snprintf(line2, sizeof(line2), "%s", mon);
    char line3[16]; // e.g., "11th"
    snprintf(line3, sizeof(line3), "%d%s", dd, day_suffix(dd));

    uint8_t rr = scale(r_, brightness_percent_);
    uint8_t gg = scale(g_, brightness_percent_);
    uint8_t bb = scale(b_, brightness_percent_);
    uint8_t ww = scale(w_, brightness_percent_);

    // Center within a 32x32 (or arbitrary) grid using the known 8px advance per glyph
    size_t rows = strip.rows();
    size_t cols = strip.cols();
    size_t h_total = 24; // three lines at 8px cadence
    size_t top = (rows > h_total) ? ((rows - h_total) / 2u) : 0u;

    size_t w1 = std::strlen(line1) * 8u;
    size_t w2 = std::strlen(line2) * 8u;
    size_t w3 = std::strlen(line3) * 8u;
    size_t left1 = (cols > w1) ? ((cols - w1) / 2u) : 0u;
    size_t left2 = (cols > w2) ? ((cols - w2) / 2u) : 0u;
    size_t left3 = (cols > w3) ? ((cols - w3) / 2u) : 0u;

    font6x6::draw_text(strip, line1, top, left1, rr, gg, bb, ww);
    font6x6::draw_text(strip, line2, top + 8u, left2, rr, gg, bb, ww);
    font6x6::draw_text(strip, line3, top + 16u, left3, rr, gg, bb, ww);
}

void SummaryPattern::update(LEDStrip& strip, uint64_t now_us) {
    (void)now_us;
    int mm, dd, wday; get_date_fields(mm, dd, wday);
    int key = (wday * 10000) + (mm * 100) + dd;
    if (last_key_ == -1 || needs_render_ || key != last_key_) {
        last_key_ = key; needs_render_ = false; render(strip);
    }
}

} // namespace leds




