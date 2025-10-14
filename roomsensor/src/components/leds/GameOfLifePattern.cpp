#include "GameOfLifePattern.h"
#include "LEDStrip.h"
#include <algorithm>
#include <strings.h>
#include "communication.h"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

// External declarations provided by wifi.cpp (keep local to this TU)
extern const uint8_t* get_device_mac(void);

namespace leds {
GameOfLifePattern::Hash256 GameOfLifePattern::compute_state_hash() const {
    // 256-bit non-cryptographic hash: mix state bits into 4x64 lanes
    Hash256 h{};
    h.x[0] = 0x6a09e667f3bcc909ULL;
    h.x[1] = 0xbb67ae8584caa73bULL;
    h.x[2] = 0x3c6ef372fe94f82bULL;
    h.x[3] = 0xa54ff53a5f1d36f1ULL;
    const size_t n = current_.size();
    uint64_t lane = 0;
    for (size_t i = 0; i < n; ++i) {
        uint64_t bit = static_cast<uint64_t>(current_[i] & 1u);
        // spread bit with index-dependent rotation
        uint64_t mix = (bit ? 0x9e3779b97f4a7c15ULL : 0ULL) ^ (static_cast<uint64_t>(i) * 0x9e3779b97f4a7c15ULL);
        uint32_t r = static_cast<uint32_t>((i * 13u) & 63u);
        mix = (mix << r) | (mix >> (64 - r));
        h.x[lane] ^= mix;
        // avalanche per step
        h.x[lane] *= 0xbf58476d1ce4e5b9ULL;
        h.x[lane] = (h.x[lane] << 31) | (h.x[lane] >> 33);
        lane = (lane + 1) & 3u;
    }
    // final mix across lanes
    uint64_t t0 = h.x[0] ^ (h.x[1] + 0x94d049bb133111ebULL);
    uint64_t t1 = h.x[1] ^ (h.x[2] + 0x2545f4914f6cdd1dULL);
    uint64_t t2 = h.x[2] ^ (h.x[3] + 0x9e3779b97f4a7c15ULL);
    uint64_t t3 = h.x[3] ^ (h.x[0] + 0x632be59bd9b4e019ULL);
    h.x[0] = t0; h.x[1] = t1; h.x[2] = t2; h.x[3] = t3;
    return h;
}

static const char* TAG = "life";

static void publish_life_complete_json(uint32_t generations, uint32_t seed, bool simple_mode, uint32_t period = 0) {
    const uint8_t* mac = get_device_mac();
    if (!mac) return;
    char mac_nosep[13];
    snprintf(mac_nosep, sizeof(mac_nosep), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    char topic[80];
    snprintf(topic, sizeof(topic), "sensor/%s/life/complete", mac_nosep);

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "generations", (double)generations);
    cJSON_AddNumberToObject(root, "seed", (double)seed);
    cJSON_AddStringToObject(root, "mode", simple_mode ? "SIMPLE" : "RANDOM");
    if (period > 0) {
        cJSON_AddNumberToObject(root, "period", (double)period);
    }
    char* json = cJSON_PrintUnformatted(root);
    if (json) {
        publish_to_topic(topic, json, 1, 0);
        cJSON_free(json);
    }
    cJSON_Delete(root);
}

static TagCollection* get_generations_tags() {
    static TagCollection* tags = nullptr;
    if (!tags) {
        tags = create_tag_collection();
        if (tags) {
            (void)add_tag_to_collection(tags, "type", "steady");
        }
    }
    return tags;
}

static void report_generations_metric(uint32_t generations) {
    TagCollection* tags = get_generations_tags();
    if (!tags) return;
    report_metric("generations", (float)generations, tags);
}

static TagCollection* get_period_tags() {
    static TagCollection* tags = nullptr;
    if (!tags) {
        tags = create_tag_collection();
        if (tags) {
            (void)add_tag_to_collection(tags, "type", "cycle");
        }
    }
    return tags;
}

static void report_period_metric(uint32_t period) {
    TagCollection* tags = get_period_tags();
    if (!tags) return;
    report_metric("period", (float)period, tags);
}

void GameOfLifePattern::reset(LEDStrip& strip, uint64_t now_us) {
    (void)now_us;
    const size_t rows = strip.rows();
    const size_t cols = strip.cols();
    const size_t total = rows * cols;
    current_.assign(total, 0);
    next_.assign(total, 0);
    generation_count_ = 0;
    steady_reported_ = false;
    // Allocate ring buffers in SPI RAM on first use
    if (!hash_ring_) {
        hash_ring_ = (Hash256*)heap_caps_calloc(kHashRingCapacity, sizeof(Hash256), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!gen_ring_) {
        gen_ring_ = (uint32_t*)heap_caps_calloc(kHashRingCapacity, sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    ring_count_ = 0;
    ring_pos_ = 0;
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
        // RANDOM - seed from current time
        uint64_t t = esp_timer_get_time();
        uint32_t seed = static_cast<uint32_t>(t ^ (t >> 32));
        initial_seed_ = seed;
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
        uint64_t t = esp_timer_get_time();
        uint32_t seed = static_cast<uint32_t>(t ^ (t >> 32));
        initial_seed_ = seed;
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
    // Check for repeating next state (period 1 or 2), regardless of mode for metrics purposes
    bool eq1 = (!prev1_.empty() && prev1_.size() == next_.size() && std::equal(next_.begin(), next_.end(), prev1_.begin()));
    bool eq2 = (!prev2_.empty() && prev2_.size() == next_.size() && std::equal(next_.begin(), next_.end(), prev2_.begin()));
    bool repeating_next_any_mode = eq1 || eq2;

    // On first time we detect a steady condition (extinction or repetition), publish metrics
    if (!steady_reported_ && (!any_alive || repeating_next_any_mode)) {
        uint32_t gens_to_report = generation_count_ + 1; // next_ would be applied next
        ESP_LOGI(TAG, "life steady detected after %u generations", (unsigned)gens_to_report);
        publish_life_complete_json(gens_to_report, initial_seed_, simple_mode_, 0);
        report_generations_metric(gens_to_report);
        steady_reported_ = true;
    }

    if (!simple_mode_ && !any_alive) {
        // Immediate reseed on extinction
        uint64_t t = esp_timer_get_time();
        uint32_t seed = static_cast<uint32_t>(t ^ (t >> 32));
        initial_seed_ = seed;
        randomize_state(rows, cols, seed);
        prev1_.clear();
        prev2_.clear();
        repeat_start_us_ = 0;
        generation_count_ = 0;
        steady_reported_ = false;
    } else {
        // Check if next frame will repeat with period 1 or 2
        bool repeating_next = false;
        if (!simple_mode_) {
            repeating_next = repeating_next_any_mode;
            if (repeating_next) {
                if (repeat_start_us_ == 0) repeat_start_us_ = now_us;
                if ((now_us - repeat_start_us_) >= 10ull * 1000 * 1000) {
                    uint64_t t = esp_timer_get_time();
                    uint32_t seed = static_cast<uint32_t>(t ^ (t >> 32));
                    initial_seed_ = seed;
                    randomize_state(rows, cols, seed);
                    prev1_.clear();
                    prev2_.clear();
                    repeat_start_us_ = 0;
                    generation_count_ = 0;
                    steady_reported_ = false;
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
        generation_count_++;

        // Cycle detection using 256-bit hash and 100-slot circular buffer
        Hash256 h = compute_state_hash();
        // Check for repeats in the buffer. We want the most recent repeat distance for logging,
        // and the total number of historical matches for cycle thresholding.
        uint32_t most_recent_distance = 0;
        uint32_t repeat_hits = 0;
        for (size_t i = 0; i < ring_count_; ++i) {
            size_t idx = (ring_pos_ + kHashRingCapacity - i - 1) % kHashRingCapacity; // iterate from most recent backwards
            if (hashes_equal(hash_ring_[idx], h)) {
                // Count every match
                repeat_hits++;
                // Capture distance to the most recent match (first one we encounter)
                if (most_recent_distance == 0) {
                    uint32_t past_gen = gen_ring_[idx];
                    if (generation_count_ > past_gen) {
                        most_recent_distance = generation_count_ - past_gen;
                    }
                }
            }
        }
        if (most_recent_distance > 0) {
            ESP_LOGI(TAG, "life hash repeat: distance=%u", (unsigned)most_recent_distance);
        }
        // Add current hash to buffer
        if (hash_ring_ && gen_ring_) {
            hash_ring_[ring_pos_] = h;
            gen_ring_[ring_pos_] = generation_count_;
            ring_pos_ = (ring_pos_ + 1) % kHashRingCapacity;
            if (ring_count_ < kHashRingCapacity) ring_count_++;
        }

        // If we have seen the same hash more than 4 times, consider cycle detected
        if (repeat_hits >= 4) {
            uint32_t period = most_recent_distance;
            ESP_LOGI(TAG, "life cycle detected: period=%u after gen=%u", (unsigned)period, (unsigned)generation_count_);
            report_period_metric(period);
            publish_life_complete_json(generation_count_, initial_seed_, simple_mode_, period);
            // Restart (reseeding if RANDOM, or just resetting SIMPLE blinker)
            reset(strip, now_us);
            render_current(strip);
            return;
        }
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



