#pragma once

#include "LEDWireEncoder.h"

namespace leds { namespace internal {

class WireEncoderWS2812 final : public LEDWireEncoder {
public:
    WireEncoderWS2812(int gpio, int enable_gpio, bool with_dma, uint32_t rmt_resolution_hz, size_t mem_block_symbols, size_t max_leds);
    ~WireEncoderWS2812();

    size_t frame_size_for(size_t rows, size_t cols) const override {
        return rows * cols * 3; // 3 bytes per LED (driver arg order R,G,B)
    }

    void encode_frame(const uint8_t* logical_rgba,
                      size_t rows,
                      size_t cols,
                      uint8_t* out_frame_bytes) const override {
        size_t count = rows * cols;
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* px = logical_rgba + i * 4;
            uint8_t r = px[0], g = px[1], b = px[2];
            // Driver API takes (r,g,b)
            out_frame_bytes[i * 3 + 0] = r;
            out_frame_bytes[i * 3 + 1] = g;
            out_frame_bytes[i * 3 + 2] = b;
        }
    }

    bool transmit_frame(const uint8_t* frame_bytes, size_t frame_size_bytes) override;
    bool is_busy() const override;

private:
    int gpio_ = -1;
    int enable_gpio_ = -1;
    bool with_dma_ = false;
    uint32_t rmt_resolution_hz_ = 10 * 1000 * 1000;
    size_t mem_block_symbols_ = 48;
    size_t max_leds_ = 0;
    void* handle_ = nullptr; // led_strip_handle_t (opaque, keep as void* here to avoid include)
    bool busy_ = false;
};

} } // namespace leds::internal


