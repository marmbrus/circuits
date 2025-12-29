#pragma once

#include <cstddef>
#include <cstdint>

namespace leds {

// Abstract interface for sending raw bytes to LED hardware.
class Transmitter {
public:
    virtual ~Transmitter() = default;

    // Transmit the provided buffer. This should be non-blocking if possible (using DMA/interrupts).
    // Returns true if transmission started, false if busy or error.
    virtual bool transmit(const uint8_t* buffer, size_t size) = 0;

    // Returns true if a transmission is currently in progress.
    virtual bool is_busy() const = 0;
    
    // Wait for current transmission to complete (blocking).
    // Useful for cleanup or synchronous updates.
    virtual void wait_for_completion() = 0;
};

} // namespace leds


