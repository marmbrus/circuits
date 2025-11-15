#pragma once

#include "LEDStrip.h"
#include <vector>
#include <cstdint>
#include <cstddef>

namespace leds {

// A memory-based implementation of the LEDStrip interface for pattern rendering
// without affecting the actual hardware strip. This allows patterns to be rendered
// to a buffer for composition or analysis without hardware writes.
class LEDBuffer final : public LEDStrip {
public:
    // Create a buffer that matches the properties of an existing strip
    explicit LEDBuffer(const LEDStrip& strip);
    
    // Create a buffer with specific properties
    LEDBuffer(int pin, size_t length, config::LEDConfig::Chip chip, size_t rows, size_t cols);
    
    ~LEDBuffer() override = default;

    // LEDStrip interface - immutable properties
    int pin() const override { return pin_; }
    size_t length() const override { return length_; }
    config::LEDConfig::Chip chip() const override { return chip_; }
    size_t rows() const override { return rows_; }
    size_t cols() const override { return cols_; }
    size_t index_for_row_col(size_t row, size_t col) const override {
        if (rows_ == 0 || cols_ == 0) return 0;
        if (row >= rows_) row = rows_ - 1;
        if (col >= cols_) col = cols_ - 1;
        size_t idx = col * rows_ + row; // column-major
        if (idx >= length_) idx = length_ - 1;
        return idx;
    }

    // LEDStrip interface - pixel operations
    bool set_pixel(size_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) override;
    bool get_pixel(size_t index, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& w) const override;
    void clear() override;

    // LEDStrip interface - transmission (no-ops for buffer)
    bool flush_if_dirty(uint64_t now_us, uint64_t max_quiescent_us = 0) override { 
        dirty_ = false; 
        return false; // Never actually transmits
    }
    bool is_transmitting() const override { return false; } // Never transmitting
    void on_transmit_complete(uint64_t now_us) override {} // No-op
    bool uses_dma() const override { return false; } // No DMA for buffers
    bool has_enable_pin() const override { return false; } // No enable pin
    void set_power_enabled(bool enabled) override {} // No-op

    // Buffer-specific operations
    void copy_from(const LEDStrip& strip);
    void copy_to(LEDStrip& strip) const;

private:
    int pin_;
    size_t length_;
    config::LEDConfig::Chip chip_;
    size_t rows_;
    size_t cols_;
    bool dirty_ = false;
    
    // Pixel storage: RGBA format, 4 bytes per pixel
    std::vector<uint8_t> pixels_;
    bool has_white_;
};

} // namespace leds
