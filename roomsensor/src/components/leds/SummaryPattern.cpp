#include "SummaryPattern.h"
#include "LEDStrip.h"
#include "font6x6.h"
#include "esp_timer.h"
#include <time.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace leds {

namespace {
    static inline uint8_t scale(uint8_t c, int percent) { 
        return static_cast<uint8_t>((static_cast<int>(c) * percent) / 100); 
    }

    // Copied from MarqueePattern
    static inline uint64_t step_interval_us(int speed_percent) {
        if (speed_percent <= 0) return 800'000ULL;
        if (speed_percent >= 100) return 30'000ULL;
        uint64_t max_us = 800'000ULL;
        uint64_t min_us = 30'000ULL;
        uint64_t span = max_us - min_us;
        return max_us - (span * static_cast<uint64_t>(speed_percent) / 100ULL);
    }
}

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

static inline void get_date_fields(int& year, int& mm, int& dd, int& wday, int& hour, int& min, int& sec) {
    time_t now = time(nullptr);
    if (now <= 0) {
        // Fallback: derive a changing but deterministic value from uptime
        uint64_t us = esp_timer_get_time();
        uint64_t days = (us / 1000000ULL) / 86400ULL;
        year = 2024;
        mm = static_cast<int>((days % 12ull) + 1ull);         // 1..12
        dd = static_cast<int>((days % 28ull) + 1ull);         // 1..28
        wday = static_cast<int>(days % 7ull);                 // 0..6 (Sun..Sat)
        hour = 12;
        min = 0;
        sec = 0;
        return;
    }
    struct tm lt; 
    localtime_r(&now, &lt);
    year = lt.tm_year + 1900;
    mm = lt.tm_mon + 1;           // 1..12
    dd = lt.tm_mday;              // 1..31
    wday = lt.tm_wday;            // 0..6 (Sun..Sat)
    hour = lt.tm_hour;
    min = lt.tm_min;
    sec = lt.tm_sec;
}

void SummaryPattern::update(LEDStrip& strip, uint64_t now_us) {
    int year, mm, dd, wday, hour, min, sec;
    get_date_fields(year, mm, dd, wday, hour, min, sec);

    State new_state = State::SUMMARY;

    // Test logic: Trigger if minute is multiple of 5
    // Countdown: if (min + 1) % 5 == 0
    // Celebration: if min % 5 == 0

    bool is_nye = (mm == 12 && dd == 31);
    bool is_ny_day = (mm == 1 && dd == 1);

    if (is_nye && hour == 23 && min == 59) {
        new_state = State::COUNTDOWN;
    } else if (is_ny_day && hour == 0 && min == 0) {
        new_state = State::CELEBRATION;
    } else {
        new_state = State::SUMMARY;
    }

    if (new_state != current_state_) {
        current_state_ = new_state;
        needs_render_ = true;
        // Reset specific states
        if (new_state == State::CELEBRATION) {
            char buf[32];
            snprintf(buf, sizeof(buf), "Happy New Year %d", year);
            celebration_msg_ = buf;
            scroll_px_ = static_cast<int>(strip.cols());
            last_step_us_ = now_us;
            last_celebration_border_toggle_us_ = now_us;
            celebration_border_state_ = false;
        } else if (new_state == State::COUNTDOWN) {
            last_border_toggle_us_ = now_us;
            border_on_ = true;
            last_countdown_sec_ = -1;
        }
    }

    if (current_state_ == State::SUMMARY) {
        int key = (wday * 10000) + (mm * 100) + dd;
        if (last_key_ == -1 || needs_render_ || key != last_key_) {
            last_key_ = key; 
            needs_render_ = false; 
            render(strip);
        }
    } else if (current_state_ == State::COUNTDOWN) {
        int seconds_left = 60 - sec;
        
        // Continuous countdown: update every second
        // Toggle border every second to create noise
        bool state_changed = false;
        if (seconds_left != last_countdown_sec_) {
            last_countdown_sec_ = seconds_left;
            state_changed = true;
            
            // Toggle border state on every second tick
            border_on_ = !border_on_; 
        }

        if (state_changed || needs_render_) {
            needs_render_ = false;
            render(strip);
        }
    } else if (current_state_ == State::CELEBRATION) {
        bool render_needed = false;

        // Alternating border update: 200ms delay
        if ((now_us - last_celebration_border_toggle_us_) >= 200000ULL) {
            last_celebration_border_toggle_us_ = now_us;
            celebration_border_state_ = !celebration_border_state_;
            render_needed = true;
        }

        // Marquee update
        uint64_t interval = step_interval_us(speed_percent_);
        if (last_step_us_ == 0 || (now_us - last_step_us_) >= interval) {
            last_step_us_ = now_us;
            ++scroll_px_;
            render_needed = true;
        }

        if (render_needed || needs_render_) {
            needs_render_ = false;
            render(strip);
        }
    }
}

void SummaryPattern::render(LEDStrip& strip) {
    // Clear
    for (size_t i = 0; i < strip.length(); ++i) strip.set_pixel(i, 0, 0, 0, 0);

    if (current_state_ == State::SUMMARY) {
        render_summary(strip);
    } else if (current_state_ == State::COUNTDOWN) {
        // Recalculate seconds left for render
        int year, mm, dd, wday, hour, min, sec;
        get_date_fields(year, mm, dd, wday, hour, min, sec);
        render_countdown(strip, 60 - sec);
    } else if (current_state_ == State::CELEBRATION) {
        render_celebration(strip);
    }
}

void SummaryPattern::render_summary(LEDStrip& strip) {
    int year, mm, dd, wday, hour, min, sec;
    get_date_fields(year, mm, dd, wday, hour, min, sec);

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

    size_t rows = strip.rows();
    size_t cols = strip.cols();
    size_t h_total = 24; 
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

void SummaryPattern::render_countdown(LEDStrip& strip, int seconds_left) {
    uint8_t rr = scale(r_, brightness_percent_);
    uint8_t gg = scale(g_, brightness_percent_);
    uint8_t bb = scale(b_, brightness_percent_);
    uint8_t ww = scale(w_, brightness_percent_);

    size_t rows = strip.rows();
    size_t cols = strip.cols();

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", seconds_left);

    size_t width = std::strlen(buf) * 8u;
    size_t height = 8u; // one line

    size_t top = (rows > height) ? ((rows - height) / 2u) : 0u;
    size_t left = (cols > width) ? ((cols - width) / 2u) : 0u;

    // Draw text
    font6x6::draw_text(strip, buf, top, left, rr, gg, bb, ww);

    // Exclusion zone: text bbox +/- 1px
    int ex_top = static_cast<int>(top) - 1;
    int ex_bottom = static_cast<int>(top + height); // +1 because bottom is inclusive in loop
    int ex_left = static_cast<int>(left) - 1;
    int ex_right = static_cast<int>(left + width);

    // Render concentric rings
    // Ring 0 is outer edge.
    // We alternate ring state based on border_on_ (which toggles every second)
    // and ring index.
    
    for (size_t i = 0; ; ++i) {
        int r_min = i;
        int r_max = static_cast<int>(rows) - 1 - i;
        int c_min = i;
        int c_max = static_cast<int>(cols) - 1 - i;

        if (r_min > r_max || c_min > c_max) break;

        // Check intersection with exclusion zone
        // If the current ring rect overlaps with exclusion zone, we stop.
        // Actually, we want to stop BEFORE we touch the exclusion zone.
        // If current ring is INSIDE the exclusion zone, stop.
        // Actually, just check if ring rect intersects/touches exclusion box.
        
        bool intersects = false;
        // Ring is a hollow rectangle. 
        // Outer rect: (r_min, c_min) to (r_max, c_max)
        // Inner rect (hole): (r_min+1, c_min+1) to (r_max-1, c_max-1)
        
        // Simple check: if any part of the ring is within exclusion zone
        if (r_min >= ex_top && r_min <= ex_bottom && 
           ((c_min >= ex_left && c_min <= ex_right) || (c_max >= ex_left && c_max <= ex_right))) intersects = true;
        if (r_max >= ex_top && r_max <= ex_bottom &&
           ((c_min >= ex_left && c_min <= ex_right) || (c_max >= ex_left && c_max <= ex_right))) intersects = true;
        
        if (c_min >= ex_left && c_min <= ex_right &&
           ((r_min >= ex_top && r_min <= ex_bottom) || (r_max >= ex_top && r_max <= ex_bottom))) intersects = true;
        if (c_max >= ex_left && c_max <= ex_right &&
           ((r_min >= ex_top && r_min <= ex_bottom) || (r_max >= ex_top && r_max <= ex_bottom))) intersects = true;

        // More robust: if ring rectangle overlaps exclusion rectangle?
        // We want to stop 1px AWAY.
        // So if ring rect overlaps with ex_rect, stop?
        // ex_rect is roughly the text box expanded by 1.
        // If r_min > ex_top && r_max < ex_bottom ...
        
        // Let's just check if the ring coordinate is inside the exclusion box.
        if (r_min >= ex_top && r_min <= ex_bottom && c_max >= ex_left && c_min <= ex_right) intersects = true;
        if (r_max >= ex_top && r_max <= ex_bottom && c_max >= ex_left && c_min <= ex_right) intersects = true;
        if (c_min >= ex_left && c_min <= ex_right && r_max >= ex_top && r_min <= ex_bottom) intersects = true;
        if (c_max >= ex_left && c_max <= ex_right && r_max >= ex_top && r_min <= ex_bottom) intersects = true;

        if (intersects) break;

        // Determine if ring is ON
        bool ring_on = ((i % 2) == 0) ^ border_on_;
        if (ring_on) {
            // Draw ring i
            for (int c = c_min; c <= c_max; ++c) {
                strip.set_pixel(strip.index_for_row_col(r_min, c), rr, gg, bb, ww);
                strip.set_pixel(strip.index_for_row_col(r_max, c), rr, gg, bb, ww);
            }
            for (int r = r_min; r <= r_max; ++r) {
                strip.set_pixel(strip.index_for_row_col(r, c_min), rr, gg, bb, ww);
                strip.set_pixel(strip.index_for_row_col(r, c_max), rr, gg, bb, ww);
            }
        }
    }
}

void SummaryPattern::render_celebration(LEDStrip& strip) {
    uint8_t rr = scale(r_, brightness_percent_);
    uint8_t gg = scale(g_, brightness_percent_);
    uint8_t bb = scale(b_, brightness_percent_);
    uint8_t ww = scale(w_, brightness_percent_);

    size_t rows = strip.rows();
    size_t cols = strip.cols();

    // Alternating border: "every other pixel is on, or off, and then they switch"
    // We can use (r+c) % 2 == 0 vs == 1 based on celebration_border_state_
    for (size_t c = 0; c < cols; ++c) {
        // Top row
        if (((0 + c) % 2 == 0) == celebration_border_state_) {
            strip.set_pixel(strip.index_for_row_col(0, c), rr, gg, bb, ww);
        }
        // Bottom row
        if (((rows - 1 + c) % 2 == 0) == celebration_border_state_) {
            strip.set_pixel(strip.index_for_row_col(rows - 1, c), rr, gg, bb, ww);
        }
    }
    for (size_t r = 1; r < rows - 1; ++r) {
        // Left column
        if (((r + 0) % 2 == 0) == celebration_border_state_) {
            strip.set_pixel(strip.index_for_row_col(r, 0), rr, gg, bb, ww);
        }
        // Right column
        if (((r + cols - 1) % 2 == 0) == celebration_border_state_) {
            strip.set_pixel(strip.index_for_row_col(r, cols - 1), rr, gg, bb, ww);
        }
    }

    if (celebration_msg_.empty()) return;

    size_t visible_cells = (cols + 7u) / 8u;
    if (visible_cells == 0) visible_cells = 1;
    std::string padding(visible_cells, ' ');
    std::string padded = padding + celebration_msg_ + padding;

    int text_px = static_cast<int>(padded.size()) * 8;
    int cycle_px = text_px + static_cast<int>(cols);
    if (cycle_px <= 0) return;

    if (scroll_px_ >= cycle_px || scroll_px_ < 0) {
        scroll_px_ = 0;
    }

    int start_x = static_cast<int>(cols) - scroll_px_;

    size_t top_row = (rows > 8u) ? ((rows - 8u) / 2u) : 0u;
    font6x6::draw_text_scrolling(strip, padded.c_str(), top_row, start_x, rr, gg, bb, ww);
}

} // namespace leds
