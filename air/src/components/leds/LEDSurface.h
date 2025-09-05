#pragma once

#include <cstddef>
#include <cstdint>

namespace leds {

// Minimal public surface for patterns: set color at (row, col), then flush.
// Internals will handle coordinate mapping and on-wire encoding/transmit.
class LEDSurface {
public:
    virtual ~LEDSurface() = default;

    virtual size_t rows() const = 0;
    virtual size_t cols() const = 0;

    // Set logical RGBA at (row, col), row-major coordinates.
    virtual void set(size_t row, size_t col, uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) = 0;

    // Clear entire logical surface to zeros.
    virtual void clear() = 0;

    // Encode and transmit current frame if not busy; returns true if enqueued.
    virtual bool flush() = 0;

    // Whether a previous transmit is still in progress.
    virtual bool is_busy() const = 0;

protected:
    LEDSurface() = default;

private:
    LEDSurface(const LEDSurface&) = delete;
    LEDSurface& operator=(const LEDSurface&) = delete;
};

} // namespace leds
