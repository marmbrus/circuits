#include "SpiTransmitter.h"
#include <cstring>
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_memory_utils.h" // for esp_ptr_dma_capable

namespace leds {

static const char* TAG = "SpiTransmitter";

SpiTransmitter::SpiTransmitter(const Config& config) : host_(config.host) {
    memset(&trans_, 0, sizeof(trans_));

    // Pre-emptively reset pins to ensure they are not held by another peripheral (e.g. RMT)
    // and are in a clean state.
    if (config.data_gpio >= 0) gpio_reset_pin((gpio_num_t)config.data_gpio);
    if (config.clock_gpio >= 0) gpio_reset_pin((gpio_num_t)config.clock_gpio);

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = config.data_gpio;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = config.clock_gpio;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 4096 * 4; 

    ESP_LOGI(TAG, "Init SPI host %d: MOSI=%d, SCLK=%d, Speed=%dHz", 
             host_, config.data_gpio, config.clock_gpio, config.clock_speed_hz);

    esp_err_t ret = spi_bus_initialize(host_, &buscfg, SPI_DMA_CH_AUTO);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPI bus initialized");
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "SPI bus already initialized. Assuming shared bus usage.");
    } else {
        ESP_LOGE(TAG, "SPI init failed: %s", esp_err_to_name(ret));
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = config.clock_speed_hz;
    devcfg.mode = 0; 
    devcfg.spics_io_num = -1; 
    devcfg.queue_size = 1;
    // devcfg.flags = 0; 

    esp_err_t err = spi_bus_add_device(host_, &devcfg, &spi_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add device: %s", esp_err_to_name(err));
    }
}

SpiTransmitter::~SpiTransmitter() {
    if (spi_handle_) {
        spi_bus_remove_device(spi_handle_);
        spi_handle_ = nullptr;
    }
    // Attempt to free bus; ignore if other devices are using it.
    spi_bus_free(host_);
}

bool SpiTransmitter::transmit(const uint8_t* buffer, size_t size) {
    if (busy_) return false;

    // Sanity check for DMA capability
    if (!esp_ptr_dma_capable(buffer)) {
        // This is critical: if buffer is not DMA capable (e.g. in PSRAM without cache flags), 
        // the driver might fail or behave oddly.
        // Warn once?
        static bool warned = false;
        if (!warned) {
            ESP_LOGW(TAG, "Transmit buffer @ %p is NOT DMA capable! This may fail.", buffer);
            warned = true;
        }
        // Proceed anyway? Or fail? Driver usually handles standard RAM.
    }

    memset(&trans_, 0, sizeof(trans_));
    trans_.length = size * 8; 
    trans_.tx_buffer = buffer;
    trans_.user = this;

    esp_err_t err = spi_device_queue_trans(spi_handle_, &trans_, 0); 
    if (err == ESP_OK) {
        busy_ = true;
        return true;
    } else {
        ESP_LOGW(TAG, "Queue failed: %s", esp_err_to_name(err));
    }
    return false;
}

bool SpiTransmitter::is_busy() const {
    if (!busy_) return false;
    
    spi_transaction_t* ret_trans = nullptr;
    esp_err_t err = spi_device_get_trans_result(spi_handle_, &ret_trans, 0);
    if (err == ESP_OK) {
        const_cast<SpiTransmitter*>(this)->busy_ = false;
        return false;
    }
    return true;
}

void SpiTransmitter::wait_for_completion() {
    if (!busy_) return;
    spi_transaction_t* ret_trans = nullptr;
    spi_device_get_trans_result(spi_handle_, &ret_trans, portMAX_DELAY);
    busy_ = false;
}

} // namespace leds
