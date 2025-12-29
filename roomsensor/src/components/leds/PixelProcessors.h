#pragma once

#include "PixelProcessor.h"

namespace leds {

class StandardPixelProcessor : public PixelProcessor {
public:
    enum class Order { RGB, GRB, BGR, RGBW, GRBW };
    
    explicit StandardPixelProcessor(Order order);
    
    size_t get_buffer_size(size_t num_leds) const override;
    void process(const Color* logical_pixels, size_t count, uint8_t* wire_buffer) override;

private:
    Order order_;
};

class Apa102PixelProcessor : public PixelProcessor {
public:
    enum class Order { BGR, RGB }; 
    
    explicit Apa102PixelProcessor(Order order = Order::BGR);

    size_t get_buffer_size(size_t num_leds) const override;
    void process(const Color* logical_pixels, size_t count, uint8_t* wire_buffer) override;

private:
    Order order_;
};

class FlipdotPixelProcessor : public PixelProcessor {
public:
    FlipdotPixelProcessor() = default;

    size_t get_buffer_size(size_t num_leds) const override;
    void process(const Color* logical_pixels, size_t count, uint8_t* wire_buffer) override;
};

} // namespace leds
