#pragma once

#include "Transmitter.h"
#include "led_strip.h" 
#include <memory>
#include "led_strip_types.h"

namespace leds {

class RmtTransmitter : public Transmitter {
public:
    struct Config {
        int gpio;
        size_t max_leds; // Added length
        uint32_t resolution_hz;
        bool with_dma;
        
        led_model_t led_model; 
        led_pixel_format_t fmt;
    };

    RmtTransmitter(const Config& config);
    ~RmtTransmitter() override;

    bool transmit(const uint8_t* buffer, size_t size) override;
    bool is_busy() const override;
    void wait_for_completion() override;

private:
    led_strip_handle_t handle_ = nullptr;
    bool has_white_ = false;
    bool busy_ = false;
};

} // namespace leds
