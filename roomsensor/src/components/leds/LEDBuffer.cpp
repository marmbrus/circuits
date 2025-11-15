#include "LEDBuffer.h"
#include "LEDConfig.h"

namespace leds {

LEDBuffer::LEDBuffer(const LEDStrip& strip) 
    : pin_(strip.pin())
    , length_(strip.length())
    , chip_(strip.chip())
    , rows_(strip.rows())
    , cols_(strip.cols())
    , has_white_(chip_ == config::LEDConfig::Chip::SK6812 || chip_ == config::LEDConfig::Chip::WS2814)
{
    // Allocate pixel storage: 4 bytes per pixel (RGBA)
    pixels_.resize(length_ * 4, 0);
}

LEDBuffer::LEDBuffer(int pin, size_t length, config::LEDConfig::Chip chip, size_t rows, size_t cols)
    : pin_(pin)
    , length_(length)
    , chip_(chip)
    , rows_(rows)
    , cols_(cols)
    , has_white_(chip == config::LEDConfig::Chip::SK6812 || chip == config::LEDConfig::Chip::WS2814)
{
    // Allocate pixel storage: 4 bytes per pixel (RGBA)
    pixels_.resize(length_ * 4, 0);
}

bool LEDBuffer::set_pixel(size_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t w) {
    if (index >= length_) return false;
    
    size_t offset = index * 4;
    bool changed = false;
    
    // For FLIPDOT, store a single on/off value in all channels
    if (chip_ == config::LEDConfig::Chip::FLIPDOT) {
        uint8_t on = ((r | g | b | w) != 0) ? 255 : 0;
        if (pixels_[offset + 0] != on) { pixels_[offset + 0] = on; changed = true; }
        if (pixels_[offset + 1] != on) { pixels_[offset + 1] = on; changed = true; }
        if (pixels_[offset + 2] != on) { pixels_[offset + 2] = on; changed = true; }
        pixels_[offset + 3] = 0; // No white for FLIPDOT
    } else {
        // Store RGBA values
        if (pixels_[offset + 0] != r) { pixels_[offset + 0] = r; changed = true; }
        if (pixels_[offset + 1] != g) { pixels_[offset + 1] = g; changed = true; }
        if (pixels_[offset + 2] != b) { pixels_[offset + 2] = b; changed = true; }
        if (has_white_) {
            if (pixels_[offset + 3] != w) { pixels_[offset + 3] = w; changed = true; }
        } else {
            pixels_[offset + 3] = 0;
        }
    }
    
    if (changed) dirty_ = true;
    return changed;
}

bool LEDBuffer::get_pixel(size_t index, uint8_t& r, uint8_t& g, uint8_t& b, uint8_t& w) const {
    if (index >= length_) return false;
    
    size_t offset = index * 4;
    r = pixels_[offset + 0];
    g = pixels_[offset + 1];
    b = pixels_[offset + 2];
    w = has_white_ ? pixels_[offset + 3] : 0;
    
    return true;
}

void LEDBuffer::clear() {
    bool any_changed = false;
    for (auto& pixel : pixels_) {
        if (pixel != 0) {
            pixel = 0;
            any_changed = true;
        }
    }
    if (any_changed) dirty_ = true;
}

void LEDBuffer::copy_from(const LEDStrip& strip) {
    size_t copy_length = std::min(length_, strip.length());
    for (size_t i = 0; i < copy_length; ++i) {
        uint8_t r, g, b, w;
        if (strip.get_pixel(i, r, g, b, w)) {
            set_pixel(i, r, g, b, w);
        }
    }
}

void LEDBuffer::copy_to(LEDStrip& strip) const {
    size_t copy_length = std::min(length_, strip.length());
    for (size_t i = 0; i < copy_length; ++i) {
        uint8_t r, g, b, w;
        if (get_pixel(i, r, g, b, w)) {
            strip.set_pixel(i, r, g, b, w);
        }
    }
}

} // namespace leds
