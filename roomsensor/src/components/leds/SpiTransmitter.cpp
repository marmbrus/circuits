#include "SpiTransmitter.h"
#include <cstring>
#include "esp_log.h"

namespace leds {

static const char* TAG = "SpiTransmitter";

SpiTransmitter::SpiTransmitter(const Config& config) {
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = config.data_gpio;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = config.clock_gpio;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 4096 * 4; // Allow large transfers

    // Initialize the SPI bus if not already initialized? 
    // We assume the caller or the system handles bus init or we do it here.
    // Caution: Multiple devices might share the bus. 
    // For now, we assume we initialize it. If it fails because it's already init, ignore?
    // Better: We should probably just add the device.
    // But we need to know if we are the bus owner.
    // For simplicity in this project, we assume we own the bus or its initialized elsewhere.
    // Let's try initializing.
    
    // Check if bus is initialized? No easy way.
    // We'll just try to initialize and ignore "already init" error?
    // Or simpler: Initialize bus in `LEDManager` or board setup?
    // Given the previous code didn't use SPI, we likely need to init it.
    
    esp_err_t ret = spi_bus_initialize(config.host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SPI bus init failed (might be already init): %s", esp_err_to_name(ret));
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = config.clock_speed_hz;
    devcfg.mode = 0; // SPI mode 0 (CPOL=0, CPHA=0) is standard for APA102
    devcfg.spics_io_num = -1; // CS not used
    devcfg.queue_size = 1;
    // devcfg.flags = SPI_DEVICE_NO_DUMMY; 

    ESP_ERROR_CHECK(spi_bus_add_device(config.host, &devcfg, &spi_handle_));
}

SpiTransmitter::~SpiTransmitter() {
    if (spi_handle_) {
        spi_bus_remove_device(spi_handle_);
    }
    // We don't free the bus because others might use it. 
    // TODO: Reference counting if needed.
}

// Callback for SPI completion
static void spi_post_trans_cb(spi_transaction_t* trans) {
    auto* self = static_cast<SpiTransmitter*>(trans->user);
    // Mark not busy? 
    // We rely on polling or waiting in this simple implementation for now,
    // or we can use the `busy_` flag if we had a way to clear it safely.
}

bool SpiTransmitter::transmit(const uint8_t* buffer, size_t size) {
    if (busy_) {
        // Check if finished?
        // spi_device_get_trans_result would block if not finished.
        // We can use polling.
        return false;
    }

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = size * 8; // length in bits
    t.tx_buffer = buffer;
    t.user = this;

    // We use polling for simplicity now to ensure non-blocking submit if queue is free?
    // Actually spi_device_queue_trans can block if queue is full.
    // We set queue_size=1.
    
    esp_err_t err = spi_device_queue_trans(spi_handle_, &t, 0); // 0 ticks wait
    if (err == ESP_OK) {
        busy_ = true;
        return true;
    }
    return false;
}

bool SpiTransmitter::is_busy() const {
    if (!busy_) return false;
    // Check if we can get the result
    spi_transaction_t* ret_trans;
    esp_err_t err = spi_device_get_trans_result(spi_handle_, &ret_trans, 0);
    if (err == ESP_OK) {
        // Completed
        const_cast<SpiTransmitter*>(this)->busy_ = false;
        return false;
    }
    return true;
}

void SpiTransmitter::wait_for_completion() {
    if (!busy_) return;
    spi_transaction_t* ret_trans;
    spi_device_get_trans_result(spi_handle_, &ret_trans, portMAX_DELAY);
    busy_ = false;
}

} // namespace leds


