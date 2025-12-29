#include "PixelProcessors.h"
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <algorithm>

namespace leds {

StandardPixelProcessor::StandardPixelProcessor(Order order) : order_(order) {}

size_t StandardPixelProcessor::get_buffer_size(size_t num_leds) const {
    switch(order_) {
        case Order::RGBW:
        case Order::GRBW: return num_leds * 4;
        default: return num_leds * 3;
    }
}

void StandardPixelProcessor::process(const Color* logical_pixels, size_t count, uint8_t* wire_buffer) {
    for (size_t i = 0; i < count; ++i) {
        const Color& c = logical_pixels[i];
        uint8_t r = c.r8();
        uint8_t g = c.g8();
        uint8_t b = c.b8();
        uint8_t w = c.w8();

        switch (order_) {
            case Order::RGB:
                wire_buffer[i*3 + 0] = r;
                wire_buffer[i*3 + 1] = g;
                wire_buffer[i*3 + 2] = b;
                break;
            case Order::GRB:
                wire_buffer[i*3 + 0] = g;
                wire_buffer[i*3 + 1] = r;
                wire_buffer[i*3 + 2] = b;
                break;
            case Order::BGR:
                wire_buffer[i*3 + 0] = b;
                wire_buffer[i*3 + 1] = g;
                wire_buffer[i*3 + 2] = r;
                break;
            case Order::RGBW:
                wire_buffer[i*4 + 0] = r;
                wire_buffer[i*4 + 1] = g;
                wire_buffer[i*4 + 2] = b;
                wire_buffer[i*4 + 3] = w;
                break;
            case Order::GRBW:
                wire_buffer[i*4 + 0] = g;
                wire_buffer[i*4 + 1] = r;
                wire_buffer[i*4 + 2] = b;
                wire_buffer[i*4 + 3] = w;
                break;
        }
    }
}

// --------------------------------------------------------------------------

Apa102PixelProcessor::Apa102PixelProcessor(Order order) : order_(order) {}

size_t Apa102PixelProcessor::get_buffer_size(size_t num_leds) const {
    size_t end_frame_bytes = (num_leds + 1) / 2 / 8 + 4; 
    return 4 + num_leds * 4 + end_frame_bytes;
}

void Apa102PixelProcessor::process(const Color* logical_pixels, size_t count, uint8_t* wire_buffer) {
    size_t offset = 0;
    
    // Start Frame
    wire_buffer[offset++] = 0x00;
    wire_buffer[offset++] = 0x00;
    wire_buffer[offset++] = 0x00;
    wire_buffer[offset++] = 0x00;

    for (size_t i = 0; i < count; ++i) {
        const Color& c = logical_pixels[i];
        // APA102: Global Brightness 5 bits
        wire_buffer[offset++] = 0xE0 | (c.dimming & 0x1F);
        
        if (order_ == Order::BGR) {
            wire_buffer[offset++] = c.b8();
            wire_buffer[offset++] = c.g8();
            wire_buffer[offset++] = c.r8();
        } else {
            wire_buffer[offset++] = c.r8();
            wire_buffer[offset++] = c.g8();
            wire_buffer[offset++] = c.b8();
        }
    }

    // End Frame
    size_t end_bytes = get_buffer_size(count) - offset;
    for (size_t i = 0; i < end_bytes; ++i) {
        wire_buffer[offset++] = 0xFF;
    }
}

// --------------------------------------------------------------------------

size_t FlipdotPixelProcessor::get_buffer_size(size_t num_leds) const {
    size_t physical = (num_leds + 2) / 3;
    return physical * 3;
}

void FlipdotPixelProcessor::process(const Color* logical_pixels, size_t count, uint8_t* wire_buffer) {
    size_t physical = (count + 2) / 3;
    for (size_t p = 0; p < physical; ++p) {
        size_t li0 = p * 3 + 0;
        size_t li1 = p * 3 + 1;
        size_t li2 = p * 3 + 2;
        
        uint8_t rch = 0, gch = 0, bch = 0;
        
        if (li0 < count) {
            const Color& c = logical_pixels[li0];
            // Inverted: non-black -> 0, black -> 255
            rch = (c.r || c.g || c.b || c.w) ? 0 : 255; 
        } else { rch = 255; }
        
        if (li1 < count) {
            const Color& c = logical_pixels[li1];
            gch = (c.r || c.g || c.b || c.w) ? 0 : 255;
        } else { gch = 255; }

        if (li2 < count) {
            const Color& c = logical_pixels[li2];
            bch = (c.r || c.g || c.b || c.w) ? 0 : 255;
        } else { bch = 255; }

        // Map first three logical dots onto (G,R,B) channels
        wire_buffer[p * 3 + 0] = gch; // G carries index 0
        wire_buffer[p * 3 + 1] = rch; // R carries index 1
        wire_buffer[p * 3 + 2] = bch; // B carries index 2
    }
}

// --------------------------------------------------------------------------

size_t Hd108PixelProcessor::get_buffer_size(size_t num_leds) const {
    // Start Frame: 16 bytes (128 bits 0s)
    // LED Frame: 8 bytes per pixel (64 bits)
    // End Frame: 64 bytes (512 bits 0s) - Increased to ensure data propagation on long strips
    return 16 + (num_leds * 8) + 64;
}

void Hd108PixelProcessor::process(const Color* logical_pixels, size_t count, uint8_t* wire_buffer) {
    size_t offset = 0;
    
    // Start Frame (16 bytes 0)
    memset(wire_buffer + offset, 0x00, 16);
    offset += 16;
    
    for (size_t i = 0; i < count; ++i) {
        const Color& c = logical_pixels[i];
        
        // Use dimming (5-bit) for per-channel gain. 
        // If dimming is 0, LEDs might be off.
        // If constructed via default constructor, dimming is 31 (max).
        // If from_rgba8, dimming is 31.
        // If user set manual color with dimming=0, then it's 0.
        
        uint8_t gain = c.dimming & 0x1F;
        
        uint8_t byte0 = static_cast<uint8_t>(
            0x80 |                  // start bit
            ((gain & 0x1F) << 2) |  // Blue gain
            ((gain & 0x1F) >> 3)    // Green gain top 2
        );
        uint8_t byte1 = static_cast<uint8_t>(
            ((gain & 0x07) << 5) |  // Green gain low 3
            (gain & 0x1F)           // Red gain
        );
        
        wire_buffer[offset++] = byte0;
        wire_buffer[offset++] = byte1;
        
        // R, G, B (16-bit Big Endian) - User reported Blue/Red swapped, changing from BGR to RGB
        wire_buffer[offset++] = static_cast<uint8_t>(c.r >> 8);
        wire_buffer[offset++] = static_cast<uint8_t>(c.r & 0xFF);
        
        wire_buffer[offset++] = static_cast<uint8_t>(c.g >> 8);
        wire_buffer[offset++] = static_cast<uint8_t>(c.g & 0xFF);
        
        wire_buffer[offset++] = static_cast<uint8_t>(c.b >> 8);
        wire_buffer[offset++] = static_cast<uint8_t>(c.b & 0xFF);
    }
    
    // End Frame (64 bytes 0)
    memset(wire_buffer + offset, 0x00, 64);
}

// --------------------------------------------------------------------------

WhiteToRgbProcessor::WhiteToRgbProcessor(std::unique_ptr<PixelProcessor> next)
    : next_(std::move(next)) {}

size_t WhiteToRgbProcessor::get_buffer_size(size_t num_leds) const {
    return next_ ? next_->get_buffer_size(num_leds) : 0;
}

void WhiteToRgbProcessor::process(const Color* logical_pixels, size_t count, uint8_t* wire_buffer) {
    if (!next_) return;

    if (scratch_.size() < count) {
        scratch_.resize(count);
    }

    // Mix white into RGB
    for (size_t i = 0; i < count; ++i) {
        const Color& in = logical_pixels[i];
        Color& out = scratch_[i];
        
        // Simple saturating addition of White to R, G, B
        uint32_t r = static_cast<uint32_t>(in.r) + in.w;
        uint32_t g = static_cast<uint32_t>(in.g) + in.w;
        uint32_t b = static_cast<uint32_t>(in.b) + in.w;

        out.r = (r > 0xFFFF) ? 0xFFFF : static_cast<uint16_t>(r);
        out.g = (g > 0xFFFF) ? 0xFFFF : static_cast<uint16_t>(g);
        out.b = (b > 0xFFFF) ? 0xFFFF : static_cast<uint16_t>(b);
        out.w = 0; // Consumed
        out.dimming = in.dimming;
    }

    next_->process(scratch_.data(), count, wire_buffer);
}

} // namespace leds
