#include "GameOfLifePattern.h"
#include "LEDStrip.h"
#include <algorithm>
#include <strings.h>

namespace leds {

void GameOfLifePattern::reset(LEDStrip& strip, uint64_t now_us) {
    (void)now_us;
    const size_t rows = strip.rows();
    const size_t cols = strip.cols();
    const size_t total = rows * cols;
    current_.assign(total, 0);
    next_.assign(total, 0);
    // Seed based on start_string_
    bool use_simple = false;
    if (!start_string_.empty()) {
        // case-insensitive compare against SIMPLE
        const char* s = start_string_.c_str();
        if (strcasecmp(s, "SIMPLE") == 0) use_simple = true;
    }
    simple_mode_ = use_simple;
    if (use_simple && rows >= 1 && cols >= 5) {
        // Blinker: three cells in a row away from edges, near top-left area
        size_t r = rows / 2; // middle row
        size_t c = 2;        // a bit away from left edge
        auto idx_of = [rows](size_t rr, size_t cc){ return cc * rows + rr; };
        current_[idx_of(r, c - 1)] = 1;
        current_[idx_of(r, c    )] = 1;
        current_[idx_of(r, c + 1)] = 1;
    } else {
        // RANDOM
        uint32_t seed = static_cast<uint32_t>(now_us ^ (now_us >> 32) ^ static_cast<uint32_t>(strip.pin() * 2654435761u));
        randomize_state(rows, cols, seed);
    }
    last_step_us_ = now_us;
    prev1_.clear();
    prev2_.clear();
    repeat_start_us_ = 0;
    render_current(strip);
}

void GameOfLifePattern::update(LEDStrip& strip, uint64_t now_us) {

    // Determine generation cadence. At speed=100, advance one generation per update
    // (bounded by RMT transmit rate) to avoid skipping generations.
    int sp = speed_percent_;
    if (sp < 0) sp = 0;
    if (sp > 100) sp = 100;
    uint64_t step_interval_us = (sp >= 100) ? 0ull : (800000ull - static_cast<uint64_t>(sp) * 6000ull); // 800ms..200ms
    if (step_interval_us > 0) {
        if (now_us - last_step_us_ < step_interval_us) {
            // Still render (e.g., on first frame after reset) without evolving
            render_current(strip);
            return;
        }
        last_step_us_ = now_us;
    }

    const size_t rows = strip.rows();
    const size_t cols = strip.cols();
    if (rows == 0 || cols == 0) return;
    const size_t total = rows * cols;
    if (current_.size() != total) {
        current_.assign(total, 0);
        next_.assign(total, 0);
        prev1_.clear();
        prev2_.clear();
        uint32_t seed = static_cast<uint32_t>(now_us ^ (now_us >> 32) ^ static_cast<uint32_t>(strip.pin() * 2654435761u));
        randomize_state(rows, cols, seed);
    }

    // Evolve using toroidal wrap-around
    auto idx_of = [rows, cols](size_t r, size_t c) -> size_t { return c * rows + r; };
    for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
            unsigned n = count_live_neighbors(rows, cols, r, c);
            uint8_t alive = current_[idx_of(r, c)];
            uint8_t next_alive = 0;
            if (alive) {
                // Survives with 2 or 3 neighbors
                next_alive = (n == 2 || n == 3) ? 1 : 0;
            } else {
                // Birth with exactly 3 neighbors
                next_alive = (n == 3) ? 1 : 0;
            }
            next_[idx_of(r, c)] = next_alive;
        }
    }

    // Detect repeats and extinct states in RANDOM mode; re-seed if extinct or after 10s of repetition
    bool any_alive = std::any_of(next_.begin(), next_.end(), [](uint8_t v){ return v != 0; });
    if (!simple_mode_ && !any_alive) {
        // Immediate reseed on extinction
        uint32_t seed = static_cast<uint32_t>((now_us ^ (now_us >> 32)) + 0x9E3779B9u);
        randomize_state(rows, cols, seed);
        prev1_.clear();
        prev2_.clear();
        repeat_start_us_ = 0;
    } else {
        // Check if next frame will repeat with period 1 or 2
        bool repeating_next = false;
        if (!simple_mode_) {
            bool eq1 = (!prev1_.empty() && prev1_.size() == next_.size() && std::equal(next_.begin(), next_.end(), prev1_.begin()));
            bool eq2 = (!prev2_.empty() && prev2_.size() == next_.size() && std::equal(next_.begin(), next_.end(), prev2_.begin()));
            repeating_next = eq1 || eq2;
            if (repeating_next) {
                if (repeat_start_us_ == 0) repeat_start_us_ = now_us;
                if ((now_us - repeat_start_us_) >= 10ull * 1000 * 1000) {
                    uint32_t seed = static_cast<uint32_t>((now_us ^ (now_us >> 32)) + 0x9E3779B9u);
                    randomize_state(rows, cols, seed);
                    prev1_.clear();
                    prev2_.clear();
                    repeat_start_us_ = 0;
                    // After reseed, render and return
                    render_current(strip);
                    return;
                }
            } else {
                repeat_start_us_ = 0;
            }
        }

        // Shift history: prev2_ <- prev1_, prev1_ <- current, then current <- next
        prev2_ = prev1_;
        prev1_ = current_;
        current_.swap(next_);
    }

    render_current(strip);
}

void GameOfLifePattern::randomize_state(size_t rows, size_t cols, uint32_t seed) {
    const size_t total = rows * cols;
    current_.assign(total, 0);
    next_.assign(total, 0);
    // Simple LCG
    uint32_t x = seed ? seed : 0xA5A5A5A5u;
    for (size_t i = 0; i < total; ++i) {
        x = x * 1664525u + 1013904223u;
        // Initialize ~35% alive to avoid immediate overcrowding
        current_[i] = ((x >> 28) & 0xF) < 6 ? 1 : 0;
    }
}

unsigned GameOfLifePattern::count_live_neighbors(size_t rows, size_t cols, size_t r, size_t c) const {
    auto idx_of = [rows](size_t rr, size_t cc) -> size_t { return cc * rows + rr; };
    auto wrap = [](size_t v, size_t max) -> size_t { return (v + max) % max; };
    unsigned cnt = 0;
    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) continue;
            size_t rr = wrap(static_cast<size_t>(static_cast<int>(r) + dr), rows);
            size_t cc = wrap(static_cast<size_t>(static_cast<int>(c) + dc), cols);
            cnt += current_[idx_of(rr, cc)] ? 1u : 0u;
        }
    }
    return cnt;
}

void GameOfLifePattern::render_current(LEDStrip& strip) const {
    const size_t rows = strip.rows();
    const size_t cols = strip.cols();
    if (rows == 0 || cols == 0) return;
    const size_t total = rows * cols;
    uint8_t r = base_r_, g = base_g_, b = base_b_, w = base_w_;
    if (brightness_percent_ < 100) {
        r = static_cast<uint8_t>((static_cast<uint16_t>(r) * brightness_percent_) / 100);
        g = static_cast<uint8_t>((static_cast<uint16_t>(g) * brightness_percent_) / 100);
        b = static_cast<uint8_t>((static_cast<uint16_t>(b) * brightness_percent_) / 100);
        w = static_cast<uint8_t>((static_cast<uint16_t>(w) * brightness_percent_) / 100);
    }
    // Row-major logical mapping; adapter/mapper will translate to physical layout
    for (size_t row = 0; row < rows; ++row) {
        for (size_t col = 0; col < cols; ++col) {
            size_t logical_i = col * rows + row; // current_ is column-major (col*rows + row)
            size_t physical_idx = strip.index_for_row_col(row, col);
            if (logical_i < current_.size()) {
                if (current_[logical_i]) {
                    strip.set_pixel(physical_idx, r, g, b, w);
                } else {
                    strip.set_pixel(physical_idx, 0, 0, 0, 0);
                }
            }
        }
    }
}

} // namespace leds



