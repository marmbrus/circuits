#pragma once

#include <cstddef>
#include <cstdint>

namespace leds { namespace internal {

// Single minimal interface: from a logical RGBA grid (row-major) to on-wire bytes and send.
class LEDWireEncoder {
public:
    virtual ~LEDWireEncoder() = default;

    // Compute total frame size in bytes for a logical rows x cols grid.
    virtual size_t frame_size_for(size_t rows, size_t cols) const = 0;

    // Encode entire frame from logical RGBA (rows*cols*4 bytes) into contiguous on-wire frame.
    virtual void encode_frame(const uint8_t* logical_rgba,
                              size_t rows,
                              size_t cols,
                              uint8_t* out_frame_bytes) const = 0;

    // Transmit the previously encoded frame providing its byte length.
    virtual bool transmit_frame(const uint8_t* frame_bytes, size_t frame_size_bytes) = 0;

    // True if a transmit is still in progress.
    virtual bool is_busy() const = 0;

protected:
    LEDWireEncoder() = default;

private:
    LEDWireEncoder(const LEDWireEncoder&) = delete;
    LEDWireEncoder& operator=(const LEDWireEncoder&) = delete;
};

} } // namespace leds::internal
