#include "esp_log.h"
#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <string.h>
#include <inttypes.h>
#include <unistd.h>

//
// Simple SPI test driver for HD108 LEDs.
// This is intentionally standalone and does NOT integrate with the existing LED subsystem.
//
// It:
//   - Configures an SPI bus at a configurable clock (default 20 MHz)
//   - Continuously runs a "sequential fill" effect: each LED fades 0->100% over 1 second,
//     and stays ON. Then the next LED begins fading in.
//
// NOTE: Adjust the GPIO assignments below to match your test wiring.
//
// NOTE
//  Order:
//     63: start bit    1 : Bit 63
//     62-58: Blue GAIN 5 : Bit 62-58
//     57-53: Green Gain 5 : Bit 57-53
//     52-48: Red Gain  5 : Bit 52-48
//     47-32: Blue      16 : B15- B0 
//     31-16: Green     16 : G15- G0
//     15-0 : RED       16 : R15- R0  (last bit on wire)
// 

static const char* TAG = "HD108_test";

// --- Hardware configuration (adjust as needed) ---

// Use SPI2_HOST by default; can be changed if that bus is already in use in your test.
static constexpr spi_host_device_t HD108_SPI_HOST = SPI2_HOST;

// GPIOs for HD108 test wiring
// Using GPIO11 (data) and GPIO12 (clock) per hardware connectors.
// SDI (data in) line -> MOSI
// CKI (clock) line -> SCLK
static constexpr int HD108_MOSI_GPIO   = 11;
static constexpr int HD108_SCLK_GPIO   = 12;
static constexpr int HD108_ENABLE_GPIO = 15; // Channel enable (set HIGH during test)

// SPI clock for HD108 test: conservative for initial validation.
static constexpr int HD108_SPI_CLOCK_HZ = 20000000; // 10 MHz

// --- Protocol constants ---

static constexpr int HD108_NUM_TEST_LEDS = 480;
static constexpr int HD108_START_FRAME_BYTES = 16;  // 128 bits of 0x00
static constexpr int HD108_END_FRAME_BYTES   = 16;  // 128 bits of 0x00
static constexpr int HD108_LED_FRAME_BYTES   = 8;   // 64 bits per pixel

static constexpr int HD108_TOTAL_BYTES =
    HD108_START_FRAME_BYTES +
    (HD108_NUM_TEST_LEDS * HD108_LED_FRAME_BYTES) +
    HD108_END_FRAME_BYTES;

// 16-bit grayscale helper
static constexpr uint16_t HD108_MAX_GRAY_16 = 0xFFFF;

// Public test entrypoint
void init_hd108()
{
    esp_err_t err;

    ESP_LOGI(TAG, "Starting HD108 test: %d LEDs, SPI host %d, MOSI=%d, SCLK=%d, EN=%d, freq=%d Hz",
             HD108_NUM_TEST_LEDS,
             static_cast<int>(HD108_SPI_HOST),
             HD108_MOSI_GPIO,
             HD108_SCLK_GPIO,
             HD108_ENABLE_GPIO,
             HD108_SPI_CLOCK_HZ);

    // --- Enable channel via GPIO 15 ---
    ESP_LOGI(TAG, "Configuring enable GPIO %d as output HIGH", HD108_ENABLE_GPIO);
    err = gpio_reset_pin(static_cast<gpio_num_t>(HD108_ENABLE_GPIO));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_reset_pin(%d) failed: %s", HD108_ENABLE_GPIO, esp_err_to_name(err));
        return;
    }
    err = gpio_set_direction(static_cast<gpio_num_t>(HD108_ENABLE_GPIO), GPIO_MODE_OUTPUT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_set_direction(%d) failed: %s", HD108_ENABLE_GPIO, esp_err_to_name(err));
        return;
    }
    err = gpio_set_level(static_cast<gpio_num_t>(HD108_ENABLE_GPIO), 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_set_level(%d, 1) failed: %s", HD108_ENABLE_GPIO, esp_err_to_name(err));
        return;
    }

    // --- Configure SPI bus ---
    ESP_LOGI(TAG, "Initializing SPI bus on host %d (with DMA auto-alloc)", static_cast<int>(HD108_SPI_HOST));
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = HD108_MOSI_GPIO;
    buscfg.miso_io_num = -1;
    buscfg.sclk_io_num = HD108_SCLK_GPIO;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = HD108_TOTAL_BYTES;

    // Use DMA so we can send frames larger than the non-DMA host maximum.
    err = spi_bus_initialize(HD108_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return;
    }

    spi_device_handle_t dev = nullptr;
    spi_device_interface_config_t devcfg = {};
    devcfg.mode = 0;  // SPI mode 0: CPOL=0, CPHA=0
    devcfg.clock_speed_hz = HD108_SPI_CLOCK_HZ;
    devcfg.spics_io_num = -1;  // no CS; HD108 only needs CLK+DATA
    devcfg.queue_size = 1;

    ESP_LOGI(TAG, "Adding SPI device: mode=%d, freq=%d Hz", devcfg.mode, devcfg.clock_speed_hz);
    err = spi_bus_add_device(HD108_SPI_HOST, &devcfg, &dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        spi_bus_free(HD108_SPI_HOST);
        return;
    }

    // For 480 LEDs, the buffer size is large.
    // Allocate the buffer on the heap (and preferably in DMA-capable memory)
    // to avoid stack overflow or memory issues.
    uint8_t* tx_buf = (uint8_t*)heap_caps_malloc(HD108_TOTAL_BYTES, MALLOC_CAP_DMA);
    if (!tx_buf) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffer of size %d", HD108_TOTAL_BYTES);
        spi_bus_remove_device(dev);
        spi_bus_free(HD108_SPI_HOST);
        return;
    }
    memset(tx_buf, 0x00, HD108_TOTAL_BYTES);

    // LED frames: high global gain for all channels; brightness will be modulated in color data
    const uint8_t gain = 0x1F; // 5-bit gain, 0-31
    const uint8_t gain_b = gain;
    const uint8_t gain_g = gain;
    const uint8_t gain_r = gain;

    for (int i = 0; i < HD108_NUM_TEST_LEDS; ++i) {
        const int base = HD108_START_FRAME_BYTES + i * HD108_LED_FRAME_BYTES;

        // Control word (16 bits)
        // Byte 0 (MSB): 1 start bit + Blue gain[4:0] + top 2 bits of Green gain
        // Byte 1 (LSB): lower 3 bits of Green gain + Red gain[4:0]
        uint8_t byte0 = static_cast<uint8_t>(
            0x80 |                  // start bit (bit 63)
            ((gain_b & 0x1F) << 2) |// bits 62-58: blue gain
            ((gain_g & 0x1F) >> 3)  // upper 2 bits of green gain
        );
        uint8_t byte1 = static_cast<uint8_t>(
            ((gain_g & 0x07) << 5) | // lower 3 bits of green gain
            (gain_r & 0x1F)          // bits 52-48: red gain
        );

        tx_buf[base + 0] = byte0;
        tx_buf[base + 1] = byte1;
    }

    // End frame: last 16 bytes already zeroed

    // --- Breathing Fade Test: All LEDs fade 0->40%->0 together, step 1ms ---
    // Max level 40% of 65535 = ~26214 (0x6666)
    static constexpr int32_t MAX_LEVEL = 0x6666;
    static constexpr int NUM_LEDS = HD108_NUM_TEST_LEDS;

    ESP_LOGI(TAG, "Starting breathing fade test (all LEDs): 0->40%%->0, 1ms/step");

    int32_t current_level = 0;
    int32_t step = 1;

    while (true) {
        // 1. Fill buffer for ALL LEDs with current_level
        uint16_t val16 = (uint16_t)current_level;
        uint8_t  val_h = (uint8_t)((val16 >> 8) & 0xFF);
        uint8_t  val_l = (uint8_t)(val16 & 0xFF);

        for (int i = 0; i < NUM_LEDS; ++i) {
            const int base = HD108_START_FRAME_BYTES + i * HD108_LED_FRAME_BYTES;
            // RGB = same value => White
            tx_buf[base + 2] = val_h; tx_buf[base + 3] = val_l;
            tx_buf[base + 4] = val_h; tx_buf[base + 5] = val_l;
            tx_buf[base + 6] = val_h; tx_buf[base + 7] = val_l;
        }

        // 2. Send
        spi_transaction_t t = {};
        t.length    = HD108_TOTAL_BYTES * 8;  // bits
        t.tx_buffer = tx_buf;

        err = spi_device_transmit(dev, &t);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "spi_device_transmit failed: %s", esp_err_to_name(err));
            break;
        }

        // 3. Update state (ping-pong)
        current_level += step;
        if (current_level >= MAX_LEVEL) {
            current_level = MAX_LEVEL;
            step = -1;
        } else if (current_level <= 0) {
            current_level = 0;
            step = 1;
        }

        // 4. Sleep 1ms
        usleep(1000); 
    }

    // If we ever break out of the loop (e.g., due to error), clean up.
    if (tx_buf) free(tx_buf);
    spi_bus_remove_device(dev);
    spi_bus_free(HD108_SPI_HOST);
}
