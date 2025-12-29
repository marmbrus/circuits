#pragma once

#include "Transmitter.h"
#include "driver/spi_master.h"

namespace leds {

class SpiTransmitter : public Transmitter {
public:
    struct Config {
        spi_host_device_t host;
        int clock_gpio;
        int data_gpio;
        int clock_speed_hz;
        int dma_channel; // 0 for auto/none?
    };

    SpiTransmitter(const Config& config);
    ~SpiTransmitter() override;

    bool transmit(const uint8_t* buffer, size_t size) override;
    bool is_busy() const override;
    void wait_for_completion() override;

private:
    spi_device_handle_t spi_handle_ = nullptr;
    bool busy_ = false;
};

} // namespace leds


