#include "PixelProcessors.h"
#include <cstring>

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

} // namespace leds
