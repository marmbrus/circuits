#pragma once

#include "Transmitter.h"
#include "driver/spi_master.h"
#include <cstring> 

namespace leds {

class SpiTransmitter : public Transmitter {
public:
    struct Config {
        spi_host_device_t host;
        int clock_gpio;
        int data_gpio;
        int clock_speed_hz;
        int dma_channel; 
    };

    SpiTransmitter(const Config& config);
    ~SpiTransmitter() override;

    bool transmit(const uint8_t* buffer, size_t size) override;
    bool is_busy() const override;
    void wait_for_completion() override;

private:
    spi_host_device_t host_;
    spi_device_handle_t spi_handle_ = nullptr;
    bool busy_ = false;
    spi_transaction_t trans_; 
};

} // namespace leds
