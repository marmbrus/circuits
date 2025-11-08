#pragma once

#include "LEDWireEncoder.h"

namespace leds { namespace internal {

// WS2814 timings compatible; RGBW on wire but mapping requires WRGB logical -> GRBW driver order
class WireEncoderWS2814 final : public LEDWireEncoder {
public:
    WireEncoderWS2814(int gpio, bool with_dma, uint32_t rmt_resolution_hz, size_t mem_block_symbols, size_t max_leds);
    ~WireEncoderWS2814();

    size_t frame_size_for(size_t rows, size_t cols) const override { return rows * cols * 4; }

    void encode_frame(const uint8_t* logical_rgba,
                      size_t rows,
                      size_t cols,
                      uint8_t* out_frame_bytes) const override {
        size_t count = rows * cols;
        for (size_t i = 0; i < count; ++i) {
            const uint8_t* px = logical_rgba + i * 4;
            uint8_t r = px[0], g = px[1], b = px[2], w = px[3];
            // Driver expects (r,g,b,w) -> on-wire GRBW; WS2814 expects WRGB ordering.
            uint8_t arg_r = r;
            uint8_t arg_g = w; // map W into G slot for driver
            uint8_t arg_b = g; // map G into B slot for driver
            uint8_t arg_w = b; // map B into W slot for driver
            out_frame_bytes[i * 4 + 0] = arg_r;
            out_frame_bytes[i * 4 + 1] = arg_g;
            out_frame_bytes[i * 4 + 2] = arg_b;
            out_frame_bytes[i * 4 + 3] = arg_w;
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
    void* handle_ = nullptr;
    bool busy_ = false;
};

} } // namespace leds::internal


