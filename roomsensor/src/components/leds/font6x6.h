#pragma once

#include <cstdint>
#include <cstddef>

namespace leds {
class LEDStrip;

namespace font6x6 {

// Render a single glyph (6x6 core within 8x8 cell) at top-left (row,col)
// Colors are RGBA (W used for RGBW strips; pass 0 for WS2812).
void draw_glyph(LEDStrip& strip, char ch, size_t top_row, size_t left_col,
                uint8_t r, uint8_t g, uint8_t b, uint8_t w);

// Render a null-terminated string starting at (top_row,left_col). Advances by
// 8 columns per glyph when there is room for a full 8x8 cell; otherwise trims
// the outer margin and still advances by 8 to maintain spacing.
size_t draw_text(LEDStrip& strip, const char* text, size_t top_row, size_t left_col,
                 uint8_t r, uint8_t g, uint8_t b, uint8_t w);

// Render scrolling text with pixel-level horizontal positioning. 'start_x'
// is a (possibly negative) global X coordinate where the first glyph would
// begin; glyphs are spaced 8 pixels apart. Any pixels falling outside the
// strip's 0..cols-1 range are clipped.
size_t draw_text_scrolling(LEDStrip& strip, const char* text, size_t top_row, int start_x,
                           uint8_t r, uint8_t g, uint8_t b, uint8_t w);

// Convenience: render a numeric digit '0'..'9' and ':' using the same glyphs
inline void draw_digit(LEDStrip& strip, int digit, size_t top_row, size_t left_col,
                       uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    char ch = (digit >= 0 && digit <= 9) ? static_cast<char>('0' + digit) : ':';
    draw_glyph(strip, ch, top_row, left_col, r, g, b, w);
}

} // namespace font6x6
} // namespace leds


