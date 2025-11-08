#pragma once

#include "LEDWireEncoder.h"

namespace leds { namespace internal {

class WireEncoderSK6812 final : public LEDWireEncoder {
public:
    WireEncoderSK6812(int gpio, bool with_dma, uint32_t rmt_resolution_hz, size_t mem_block_symbols, size_t max_leds);
    ~WireEncoderSK6812();

    size_t frame_size_for(size_t rows, size_t cols) const override {
        return rows * cols * 4; // RGBW per LED
    }

    void encode_frame(const uint8_t* logical_rgba,
                      size_t rows,
                      size_t cols,
                      uint8_t* out_frame_bytes) const override {
        size_t count = rows * cols;
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* px = logical_rgba + i * 4;
            // Driver API takes (r,g,b,w)
            out_frame_bytes[i * 4 + 0] = px[0];
            out_frame_bytes[i * 4 + 1] = px[1];
            out_frame_bytes[i * 4 + 2] = px[2];
            out_frame_bytes[i * 4 + 3] = px[3];
        }
    }

    bool transmit_frame(const uint8_t* frame_bytes, size_t frame_size_bytes) override;
    bool is_busy() const override;

private:
    int gpio_ = -1;
    bool with_dma_ = false;
    uint32_t rmt_resolution_hz_ = 10 * 1000 * 1000;
    size_t mem_block_symbols_ = 48;
    size_t max_leds_ = 0;
    void* handle_ = nullptr; // led_strip_handle_t
    bool busy_ = false;
};

} } // namespace leds::internal


