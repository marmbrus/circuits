#pragma once

#include "Color.h"
#include <cstddef>
#include <cstdint>

namespace leds {

// Responsible for converting high-fidelity logical pixels (16-bit Color)
// into the raw byte stream required by a specific LED controller protocol.
class PixelProcessor {
public:
    virtual ~PixelProcessor() = default;

    // Calculate total buffer size needed for N LEDs
    virtual size_t get_buffer_size(size_t num_leds) const = 0;

    // Convert logical colors to wire format
    // wire_buffer must be at least get_buffer_size(count) bytes large.
    virtual void process(const Color* logical_pixels, size_t count, uint8_t* wire_buffer) = 0;
};

} // namespace leds


