#pragma once

#include "PixelProcessor.h"
#include <memory>
#include <vector>

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

class Hd108PixelProcessor : public PixelProcessor {
public:
    Hd108PixelProcessor() = default;

    size_t get_buffer_size(size_t num_leds) const override;
    void process(const Color* logical_pixels, size_t count, uint8_t* wire_buffer) override;
};

class WhiteToRgbProcessor : public PixelProcessor {
public:
    explicit WhiteToRgbProcessor(std::unique_ptr<PixelProcessor> next);

    size_t get_buffer_size(size_t num_leds) const override;
    void process(const Color* logical_pixels, size_t count, uint8_t* wire_buffer) override;

private:
    std::unique_ptr<PixelProcessor> next_;
    // Mutable scratch buffer to avoid reallocation
    std::vector<Color> scratch_;
};

} // namespace leds
