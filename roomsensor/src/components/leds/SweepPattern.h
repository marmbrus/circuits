#pragma once

#include "LEDPattern.h"
#include <cstddef>
#include <vector>

namespace leds {

// Sweeps color/brightness changes across the strip pixel by pixel
class SweepPattern final : public LEDPattern {
public:
    const char* name() const override { return "SWEEP"; }
    void reset(LEDStrip& strip, uint64_t now_us) override;
    void update(LEDStrip& strip, uint64_t now_us) override;

    void set_solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override;
    void set_brightness_percent(int brightness_percent) override;
    void set_speed_percent(int speed_percent) override;

private:
    // Target color and brightness (requested)
    uint8_t target_r_ = 0, target_g_ = 0, target_b_ = 0, target_w_ = 0;
    int target_brightness_percent_ = 100;

    // Base color/brightness currently on the strip (for unswept pixels)
    uint8_t base_r_ = 0, base_g_ = 0, base_b_ = 0, base_w_ = 0;
    int base_brightness_percent_ = 0; // 0 => off by default

    // Last target we started sweeping toward (for change detection)
    uint8_t last_target_r_ = 0, last_target_g_ = 0, last_target_b_ = 0, last_target_w_ = 0;
    int last_target_brightness_percent_ = 0;

    // Current sweep state
    size_t last_applied_count_ = 0;   // number of leading pixels already updated to target color
    bool sweeping_ = false;           // Whether a sweep is in progress
    uint64_t sweep_start_us_ = 0;     // Time when the current sweep started
    uint64_t total_sweep_time_us_ = 0;// Precomputed sweep duration in microseconds
    
    // Speed control: 1..100 seconds total; 0 => treated as 1 second
    int speed_percent_ = 50;
    
    // Strip length cache
    size_t strip_length_ = 0;
    
    // Helper to check if parameters changed
    bool has_changed() const;
    
    // Helper to apply brightness scaling
    void apply_color(uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& w) const;
};

} // namespace leds


