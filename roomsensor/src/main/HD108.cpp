#include "esp_log.h"
#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <string.h>
#include <inttypes.h>

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

    // --- Sequential fill loop: one LED fades 0->100% over 1s and stays ON, then next LED starts ---
    static constexpr uint64_t FADE_PERIOD_US = 1000000ULL; // 1 second per LED fade-in
    static constexpr uint64_t FPS_REPORT_US  = 10000000ULL; // report every 10 seconds
    static constexpr int NUM_LEDS = HD108_NUM_TEST_LEDS;

    ESP_LOGI(TAG, "Entering sequential fill loop: each LED fades 0->100%% over %" PRIu64 " us and stays ON", FADE_PERIOD_US);

    uint64_t last_cycle_start_us = esp_timer_get_time();
    uint64_t last_report_us = last_cycle_start_us;
    uint32_t frames_since_report = 0;
    int current_led_index = 0;
    bool logged_transition = false;

    while (true) {
        uint64_t now_us = esp_timer_get_time();
        
        // Calculate how far we are into the current LED's fade period
        uint64_t elapsed_in_cycle = now_us - last_cycle_start_us;
        
        // Move to next LED if period finished
        if (elapsed_in_cycle >= FADE_PERIOD_US) {
            // Once an LED finishes fading in, it stays fully ON.
            // Move to the next LED.
            current_led_index++;
            
            // If we've filled the whole strip, reset (or just stop, depending on desire).
            // Here we'll wrap around and clear everything to start over for a continuous test.
            if (current_led_index >= NUM_LEDS) {
                current_led_index = 0;
                // Clear the buffer (except control headers) to restart from darkness
                for (int i = 0; i < NUM_LEDS; ++i) {
                    const int base = HD108_START_FRAME_BYTES + i * HD108_LED_FRAME_BYTES;
                    memset(&tx_buf[base + 2], 0, 6); // clear R, G, B bytes
                }
                ESP_LOGI(TAG, "Strip full! Resetting to start.");
            }

            last_cycle_start_us = now_us;
            elapsed_in_cycle = 0;
            logged_transition = false; 
        }

        // Calculate brightness phase (0..1) for the current fading LED
        float phase = static_cast<float>(elapsed_in_cycle) / static_cast<float>(FADE_PERIOD_US);
        if (phase > 1.0f) phase = 1.0f; // clamp

        // Linear fade in: 0 -> 1
        uint16_t fade_level = static_cast<uint16_t>(phase * 65535.0f + 0.5f);

        // Check if "fully on" to log
        if (!logged_transition && phase > 0.95f) {
            ESP_LOGI(TAG, "LED %d is fully ON (moving to next LED soon)", current_led_index);
            logged_transition = true;
        }

        // Update LEDs:
        // - Indices < current_led_index: Fully ON (0xFFFF)
        // - Index == current_led_index: Fading in (fade_level)
        // - Indices > current_led_index: OFF (0x0000)
        // Optimization: We only need to update the current one actively, previous ones are already set to max
        // if we updated them at end of cycle, but to be robust we can set the active one every frame.
        
        // Actually, since we rewrite the buffer every time in the loop (or rely on persistence), 
        // let's explicitly set the current LED. Previous LEDs retain their last state in the buffer 
        // (which should be 0xFFFF from the end of their cycle) if we don't clear it.
        // Wait, `memset` was only at the start.
        // So:
        // 1. Ensure the PREVIOUS LED is fully set to 0xFFFF (fix up potential floating point drift at end of its cycle).
        if (current_led_index > 0) {
             const int prev_base = HD108_START_FRAME_BYTES + (current_led_index - 1) * HD108_LED_FRAME_BYTES;
             tx_buf[prev_base + 2] = 0xFF; tx_buf[prev_base + 3] = 0xFF; // R
             tx_buf[prev_base + 4] = 0xFF; tx_buf[prev_base + 5] = 0xFF; // G
             tx_buf[prev_base + 6] = 0xFF; tx_buf[prev_base + 7] = 0xFF; // B
        }

        // 2. Update CURRENT LED with fade level
        const int curr_base = HD108_START_FRAME_BYTES + current_led_index * HD108_LED_FRAME_BYTES;
        tx_buf[curr_base + 2] = static_cast<uint8_t>((fade_level >> 8) & 0xFF);
        tx_buf[curr_base + 3] = static_cast<uint8_t>(fade_level & 0xFF);
        tx_buf[curr_base + 4] = static_cast<uint8_t>((fade_level >> 8) & 0xFF);
        tx_buf[curr_base + 5] = static_cast<uint8_t>(fade_level & 0xFF);
        tx_buf[curr_base + 6] = static_cast<uint8_t>((fade_level >> 8) & 0xFF);
        tx_buf[curr_base + 7] = static_cast<uint8_t>(fade_level & 0xFF);

        // 3. (Optional) Clear NEXT LED to ensure it starts black (if we just wrapped/reset)
        // But the reset logic handles that.

        spi_transaction_t t = {};
        t.length    = HD108_TOTAL_BYTES * 8;  // bits
        t.tx_buffer = tx_buf;

        err = spi_device_transmit(dev, &t);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "spi_device_transmit failed: %s", esp_err_to_name(err));
            break;
        }

        frames_since_report++;
        uint64_t elapsed_report_us = now_us - last_report_us;
        if (elapsed_report_us >= FPS_REPORT_US) {
            float elapsed_s = static_cast<float>(elapsed_report_us) / 1000000.0f;
            float fps       = static_cast<float>(frames_since_report) / elapsed_s;
            ESP_LOGI(TAG, "HD108 sequential: frames=%" PRIu32 " over %.3fs -> %.1f FPS (current LED=%d)",
                     frames_since_report, elapsed_s, fps, current_led_index);
            last_report_us      = now_us;
            frames_since_report = 0;
        }
    }

    // If we ever break out of the loop (e.g., due to error), clean up.
    if (tx_buf) free(tx_buf);
    spi_bus_remove_device(dev);
    spi_bus_free(HD108_SPI_HOST);
}
