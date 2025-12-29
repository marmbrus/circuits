#pragma once

#include <cstdint>
#include <cstddef>
#include "LEDConfig.h"
#include "Color.h"

namespace leds {

// Abstraction for a single LED strip.
// Responsibilities:
// - Own the underlying hardware resources (RMT channel / SPI device)
// - Provide pixel set/get operations with color-order awareness
// - Track a dirty bit and minimize transmissions
class LEDStrip {
public:
    virtual ~LEDStrip() = default;

    // Immutable properties
    virtual int pin() const = 0;
    virtual size_t length() const = 0; // number of addressable LEDs
    virtual config::LEDConfig::Chip chip() const = 0;
    // 2D grid geometry. Patterns can treat the strip as a rows x cols grid.
    virtual size_t rows() const = 0; 
    virtual size_t cols() const = 0; 
    // Translate (row, col) -> linear index respecting column-major collection order.
    virtual size_t index_for_row_col(size_t row, size_t col) const = 0;

    // Pixel accessors
    // Returns true if the stored pixel value changed (i.e., would mark strip dirty), false otherwise.
    virtual bool set_pixel(size_t index, const Color& color) = 0;
    virtual Color get_pixel(size_t index) const = 0;

    // Legacy helpers
    bool set_pixel(size_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) {
        return set_pixel(index, Color::from_rgba8(r, g, b, w));
    }
    bool get_pixel(size_t index, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& w) const {
        Color c = get_pixel(index);
        r = c.r8(); g = c.g8(); b = c.b8(); w = c.w8();
        return true;
    }

    // Global operations
    virtual void clear() = 0; 

    // Transmission control
    virtual bool flush_if_dirty(uint64_t now_us, uint64_t max_quiescent_us = 10ull * 1000 * 1000) = 0;
    virtual bool is_transmitting() const = 0;
    virtual void on_transmit_complete(uint64_t now_us) = 0;

    // Resource info
    virtual bool uses_dma() const = 0; 

    // Optional hardware power control (enable pin).
    virtual bool has_enable_pin() const = 0;
    virtual void set_power_enabled(bool on) = 0;
};

} // namespace leds
