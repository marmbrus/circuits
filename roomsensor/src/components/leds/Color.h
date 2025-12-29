#pragma once

#include <cstdint>
#include <algorithm>

namespace leds {

struct Color {
    uint16_t r;
    uint16_t g;
    uint16_t b;
    uint16_t w;      // White channel for RGBW
    uint8_t dimming; // 5-bit Global brightness (0-31) for APA102/SK9822

    Color() : r(0), g(0), b(0), w(0), dimming(31) {}
    Color(uint16_t r, uint16_t g, uint16_t b, uint16_t w = 0, uint8_t dim = 31)
        : r(r), g(g), b(b), w(w), dimming(dim) {}

    // Helpers to convert to 8-bit
    uint8_t r8() const { return static_cast<uint8_t>(r >> 8); }
    uint8_t g8() const { return static_cast<uint8_t>(g >> 8); }
    uint8_t b8() const { return static_cast<uint8_t>(b >> 8); }
    uint8_t w8() const { return static_cast<uint8_t>(w >> 8); }

    // Helpers to create from 8-bit
    static Color from_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t w = 0) {
        // Promote 8-bit to 16-bit. 
        // 0 -> 0, 255 -> 65280. To get full range 65535, we could multiply by 257 (0x0101).
        // For simplicity and speed, we use shift, consistent with r8().
        return Color(static_cast<uint16_t>(r) << 8,
                     static_cast<uint16_t>(g) << 8,
                     static_cast<uint16_t>(b) << 8,
                     static_cast<uint16_t>(w) << 8,
                     31);
    }
    
    bool operator==(const Color& other) const {
        return r == other.r && g == other.g && b == other.b && w == other.w && dimming == other.dimming;
    }
    bool operator!=(const Color& other) const { return !(*this == other); }
};

} // namespace leds


