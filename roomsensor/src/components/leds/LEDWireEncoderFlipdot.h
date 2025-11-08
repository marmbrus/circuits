#pragma once

#include "LEDWireEncoder.h"

namespace leds { namespace internal {

class WireEncoderFlipdot final : public LEDWireEncoder {
public:
    WireEncoderFlipdot(int gpio, bool with_dma, uint32_t rmt_resolution_hz, size_t mem_block_symbols, size_t max_leds);
    ~WireEncoderFlipdot();

    size_t frame_size_for(size_t rows, size_t cols) const override {
        size_t logical = rows * cols;
        size_t physical = (logical + 2) / 3; // 3 logical -> 1 physical
        return physical * 3; // RGB channels carry three logical dots
    }

    void encode_frame(const uint8_t* logical_rgba,
                      size_t rows,
                      size_t cols,
                      uint8_t* out_frame_bytes) const override {
        size_t logical = rows * cols;
        size_t physical = (logical + 2) / 3;
        for (size_t p = 0; p < physical; ++p) {
            size_t li0 = p * 3 + 0;
            size_t li1 = p * 3 + 1;
            size_t li2 = p * 3 + 2;
            uint8_t rch = 0, gch = 0, bch = 0;
            if (li0 < logical) {
                const uint8_t* px = logical_rgba + li0 * 4;
                rch = (px[0] | px[1] | px[2] | px[3]) ? 0 : 255; // inverted: non-black -> 0, black -> 255
            }
            if (li1 < logical) {
                const uint8_t* px = logical_rgba + li1 * 4;
                gch = (px[0] | px[1] | px[2] | px[3]) ? 0 : 255;
            }
            if (li2 < logical) {
                const uint8_t* px = logical_rgba + li2 * 4;
                bch = (px[0] | px[1] | px[2] | px[3]) ? 0 : 255;
            }
            // Map first three logical dots onto (G,R,B) channels to match observed hardware ordering
            out_frame_bytes[p * 3 + 0] = gch; // channel G carries index 0
            out_frame_bytes[p * 3 + 1] = rch; // channel R carries index 1
            out_frame_bytes[p * 3 + 2] = bch; // channel B carries index 2
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
