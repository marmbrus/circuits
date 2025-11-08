#pragma once

#include "LEDPattern.h"
#include <vector>
#include <string>

namespace leds {

class GameOfLifePattern final : public LEDPattern {
public:
    const char* name() const override { return "LIFE"; }
    void reset(LEDStrip& strip, uint64_t now_us) override;
    void update(LEDStrip& strip, uint64_t now_us) override;

    void set_speed_percent(int speed_percent) override { speed_percent_ = speed_percent; }
    void set_brightness_percent(int brightness_percent) override {
        if (brightness_percent < 0) brightness_percent = 0;
        if (brightness_percent > 100) brightness_percent = 100;
        brightness_percent_ = brightness_percent;
    }
    void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override {
        // If all zeros provided (typical when not configured), keep existing defaults
        if ((r | g | b | w) != 0) { base_r_ = r; base_g_ = g; base_b_ = b; base_w_ = w; }
    }
    void set_start_string(const char* start) override { start_string_ = start ? start : ""; }

private:
    enum class StartMode {
        RANDOM,
        SIMPLE,
        NUMERIC,
        RLE,
    };
    struct StartSpec {
        StartMode mode = StartMode::RANDOM;
        uint32_t seed = 0;       // valid when mode == NUMERIC
        const char* rle = nullptr; // points into start_string_.c_str() after any prefix/whitespace
    };

    StartSpec parse_start_spec() const;
    bool apply_rle_seed(const char* rle, size_t rows, size_t cols);
    void compute_rle_dimensions(const char* rle, size_t& out_width, size_t& out_height) const;
    void randomize_state(size_t rows, size_t cols, uint32_t seed);
    unsigned count_live_neighbors(size_t rows, size_t cols, size_t r, size_t c) const;
    void render_current(LEDStrip& strip) const;
    struct Hash256 { uint64_t x[4]; };
    Hash256 compute_state_hash() const;
    static bool hashes_equal(const Hash256& a, const Hash256& b) {
        return a.x[0] == b.x[0] && a.x[1] == b.x[1] && a.x[2] == b.x[2] && a.x[3] == b.x[3];
    }

    std::vector<uint8_t> current_; // 0 or 1 per cell
    std::vector<uint8_t> next_;
    std::vector<uint8_t> prev1_;
    std::vector<uint8_t> prev2_;
    uint64_t last_step_us_ = 0;
    uint64_t repeat_start_us_ = 0;
    uint32_t generation_count_ = 0;
    bool steady_reported_ = false;
    int speed_percent_ = 50; // 0..100
    int brightness_percent_ = 100; // 0..100
    uint8_t base_r_ = 255, base_g_ = 255, base_b_ = 255, base_w_ = 0;
    std::string start_string_;
    bool simple_mode_ = false;
    uint32_t initial_seed_ = 0; // seed used to create the run's initial random state
    // Track last applied life config generation to restart on change
    uint32_t last_life_generation_ = 0;
    // Cycle detection ring buffer allocated in SPI RAM (stores hashes and generation indices)
    static constexpr size_t kHashRingCapacity = 1000;
    Hash256* hash_ring_ = nullptr;
    uint32_t* gen_ring_ = nullptr;
    size_t ring_count_ = 0; // number of valid entries (<= kHashRingCapacity)
    size_t ring_pos_ = 0;   // next position to write
};

} // namespace leds



