#pragma once

#include <cstdint>
#include <cstddef>

namespace leds {

// Logical LED chip type; should align with config::LEDConfig::Chip but avoids including configuration here
enum class LEDChip {
    WS2812 = 0, // GRB (RGB) with implicit W=0
    SK6812,     // RGBW
};

// Abstraction for a single LED strip backed by RMT hardware.
// Responsibilities:
// - Own the underlying RMT channel and (optionally) DMA resources
// - Provide pixel set/get operations with color-order awareness
// - Track a dirty bit and minimize transmissions; also force a refresh at a low cadence (~10s)
// - Expose non-blocking transmit API and report whether the previous frame is still in-flight
class LEDStrip {
public:
    virtual ~LEDStrip() = default;

    // Immutable properties
    virtual int pin() const = 0;
    virtual size_t length() const = 0; // number of addressable LEDs
    virtual LEDChip chip() const = 0;
    // 2D grid geometry. Patterns can treat the strip as a rows x cols grid.
    // LEDs are laid out in column-major order (index = col * rows + row).
    virtual size_t rows() const = 0; // number of rows in the logical grid (>= 1)
    virtual size_t cols() const = 0; // number of columns in the logical grid (>= 1)
    // Translate (row, col) -> linear index respecting column-major collection order.
    virtual size_t index_for_row_col(size_t row, size_t col) const = 0;

    // Pixel accessors: values are logical 8-bit channels; implementation handles chip-specific ordering.
    // Returns true if the stored pixel value changed (i.e., would mark strip dirty), false otherwise.
    virtual bool set_pixel(size_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) = 0;
    virtual bool get_pixel(size_t index, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& w) const = 0;

    // Global operations
    virtual void clear() = 0;                 // set all pixels to 0; marks strip dirty only if some pixel changed
    // Note: brightness is NOT a global strip property. It is implemented by patterns (e.g., a dimming
    // pattern that may choose to light only a subset of LEDs for very low luminosity on dense strips).

    // Transmission control
    // - flush_if_dirty: schedule a non-blocking transmit if the strip is dirty OR if the last forced refresh
    //   was over 'max_quiescent_us' ago. Returns true if a transmit was enqueued.
    // - is_transmitting: true while RMT/DMA is actively sending the previously enqueued frame.
    // - on_transmit_complete: should be invoked by ISR/RMT event handler when the frame completes.
    virtual bool flush_if_dirty(uint64_t now_us, uint64_t max_quiescent_us = 10ull * 1000 * 1000) = 0;
    virtual bool is_transmitting() const = 0;
    virtual void on_transmit_complete(uint64_t now_us) = 0;

    // Resource info
    virtual bool uses_dma() const = 0; // current mode

    // Optional hardware power control (enable pin). When present, driving the pin HIGH powers LEDs on,
    // and driving it LOW powers LEDs off.
    virtual bool has_enable_pin() const = 0;
    virtual void set_power_enabled(bool on) = 0;
};

} // namespace leds


