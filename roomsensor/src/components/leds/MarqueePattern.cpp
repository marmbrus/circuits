#include "MarqueePattern.h"

#include "LEDStrip.h"
#include "font6x6.h"

namespace leds {

namespace {
static inline uint8_t scale_channel(uint8_t c, int percent) {
    if (percent <= 0) return 0;
    if (percent >= 100) return c;
    return static_cast<uint8_t>((static_cast<int>(c) * percent) / 100);
}

// Map speed_percent (0..100) to an update interval for pixel steps.
// 0   -> 800ms per pixel (very slow)
// 100 -> 30ms per pixel (fast)
static inline uint64_t step_interval_us(int speed_percent) {
    if (speed_percent <= 0) return 800'000ULL;
    if (speed_percent >= 100) return 30'000ULL;
    uint64_t max_us = 800'000ULL;
    uint64_t min_us = 30'000ULL;
    uint64_t span = max_us - min_us;
    return max_us - (span * static_cast<uint64_t>(speed_percent) / 100ULL);
}
} // namespace

void MarqueePattern::set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    r_ = r;
    g_ = g;
    b_ = b;
    w_ = w;
}

void MarqueePattern::set_brightness_percent(int brightness_percent) {
    if (brightness_percent < 0) brightness_percent = 0;
    if (brightness_percent > 100) brightness_percent = 100;
    brightness_percent_ = brightness_percent;
}

void MarqueePattern::set_speed_percent(int speed_percent) {
    if (speed_percent < 0) speed_percent = 0;
    if (speed_percent > 100) speed_percent = 100;
    speed_percent_ = speed_percent;
}

void MarqueePattern::set_start_string(const char* start) {
    if (start && *start) {
        message_ = start;
    } else {
        message_.clear();
    }
    // Force scroll to restart from the beginning on next reset/render.
    scroll_px_ = 0;
}

void MarqueePattern::reset(LEDStrip& strip, uint64_t now_us) {
    // Start with the text already aligned to the left edge so it becomes
    // visible immediately instead of spending a long time fully off-screen.
    scroll_px_ = static_cast<int>(strip.cols());
    last_step_us_ = now_us;
    render(strip);
}

void MarqueePattern::update(LEDStrip& strip, uint64_t now_us) {
    uint64_t interval = step_interval_us(speed_percent_);
    if (last_step_us_ == 0 || (now_us - last_step_us_) >= interval) {
        last_step_us_ = now_us;
        // Advance by at most one pixel per tick; render() will wrap as needed.
        ++scroll_px_;
        render(strip);
    }
}

void MarqueePattern::render(LEDStrip& strip) {
    size_t rows = strip.rows();
    size_t cols = strip.cols();
    if (rows == 0 || cols == 0) return;

    // Clear strip first
    for (size_t i = 0; i < strip.length(); ++i) {
        strip.set_pixel(i, 0, 0, 0, 0);
    }

    // If there is no message, nothing to draw.
    if (message_.empty()) return;

    // Each glyph occupies an 8-column cell (see font6x6::draw_text). Build a
    // padded message so we scroll in from the right and out to the left, with
    // a blank gap before/after.
    size_t visible_cells = (cols + 7u) / 8u;
    if (visible_cells == 0) visible_cells = 1;
    std::string padding(visible_cells, ' ');
    std::string padded = padding + message_ + padding;

    int text_px = static_cast<int>(padded.size()) * 8;
    int cycle_px = text_px + static_cast<int>(cols);
    if (cycle_px <= 0) return;

    // Wrap scroll_px_ into a valid range [0, cycle_px).
    if (scroll_px_ >= cycle_px || scroll_px_ < 0) {
        scroll_px_ = 0;
    }

    // Starting X for the first glyph such that increasing scroll_px_ moves
    // text from right to left across the display.
    int start_x = static_cast<int>(cols) - scroll_px_;

    uint8_t rr = scale_channel(r_, brightness_percent_);
    uint8_t gg = scale_channel(g_, brightness_percent_);
    uint8_t bb = scale_channel(b_, brightness_percent_);
    uint8_t ww = scale_channel(w_, brightness_percent_);

    size_t top_row = (rows > 8u) ? ((rows - 8u) / 2u) : 0u;
    font6x6::draw_text_scrolling(strip, padded.c_str(), top_row, start_x, rr, gg, bb, ww);
}

} // namespace leds




