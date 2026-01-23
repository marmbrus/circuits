#include "cmd_hd108.h"

#include "argtable3/argtable3.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdint>

// HD108 single-shot debug command.
// Copies the core SPI framing from main/HD108.cpp but sends one frame that sets
// the first 100 pixels to the provided values.

static const char *TAG = "cmd_hd108";

static constexpr spi_host_device_t HD108_SPI_HOST = SPI2_HOST;
static constexpr int HD108_MOSI_GPIO   = 11;
static constexpr int HD108_SCLK_GPIO   = 12;
static constexpr int HD108_ENABLE_GPIO = 15;
static constexpr int HD108_SPI_CLOCK_HZ = 20000000;

static constexpr int HD108_NUM_LEDS = 480;
static constexpr int HD108_ACTIVE_LEDS = 100;
static constexpr int HD108_START_FRAME_BYTES = 16;
static constexpr int HD108_END_FRAME_BYTES   = 64; // Increased to ensure data latches for all 480 LEDs
static constexpr int HD108_LED_FRAME_BYTES   = 8;
static constexpr int HD108_TOTAL_BYTES =
    HD108_START_FRAME_BYTES +
    (HD108_NUM_LEDS * HD108_LED_FRAME_BYTES) +
    HD108_END_FRAME_BYTES;

static spi_device_handle_t s_spi_dev = nullptr;

static esp_err_t hd108_send_frame(uint8_t gain_b, uint8_t gain_g, uint8_t gain_r,
                                  uint16_t blue, uint16_t green, uint16_t red)
{
    esp_err_t err = ESP_OK;

    if (s_spi_dev == nullptr) {
        // One-time initialization of GPIO and SPI bus
        
        // Enable channel output
        err = gpio_reset_pin(static_cast<gpio_num_t>(HD108_ENABLE_GPIO));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "gpio_reset_pin(%d) failed: %s", HD108_ENABLE_GPIO, esp_err_to_name(err));
            return err;
        }
        err = gpio_set_direction(static_cast<gpio_num_t>(HD108_ENABLE_GPIO), GPIO_MODE_OUTPUT);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "gpio_set_direction(%d) failed: %s", HD108_ENABLE_GPIO, esp_err_to_name(err));
            return err;
        }
        err = gpio_set_level(static_cast<gpio_num_t>(HD108_ENABLE_GPIO), 1);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "gpio_set_level(%d, 1) failed: %s", HD108_ENABLE_GPIO, esp_err_to_name(err));
            return err;
        }
        
        // Allow power/level shifters to stabilize
        vTaskDelay(pdMS_TO_TICKS(10));

        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = HD108_MOSI_GPIO;
        buscfg.miso_io_num = -1;
        buscfg.sclk_io_num = HD108_SCLK_GPIO;
        buscfg.quadwp_io_num = -1;
        buscfg.quadhd_io_num = -1;
        buscfg.max_transfer_sz = HD108_TOTAL_BYTES;

        err = spi_bus_initialize(HD108_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
            return err;
        }

        spi_device_interface_config_t devcfg = {};
        devcfg.mode = 0;
        devcfg.clock_speed_hz = HD108_SPI_CLOCK_HZ;
        devcfg.spics_io_num = -1;
        devcfg.queue_size = 1;

        err = spi_bus_add_device(HD108_SPI_HOST, &devcfg, &s_spi_dev);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
            spi_bus_free(HD108_SPI_HOST);
            return err;
        }
    } else {
        // Ensure enabled is still high
        gpio_set_level(static_cast<gpio_num_t>(HD108_ENABLE_GPIO), 1);
    }

    // Allocate buffer for this transaction
    uint8_t *tx_buf = static_cast<uint8_t*>(heap_caps_malloc(HD108_TOTAL_BYTES, MALLOC_CAP_DMA));
    if (!tx_buf) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffer of size %d", HD108_TOTAL_BYTES);
        return ESP_ERR_NO_MEM;
    }
    memset(tx_buf, 0x00, HD108_TOTAL_BYTES);

    // Control word bytes derived from HD108 datasheet layout.
    const uint8_t byte0 = static_cast<uint8_t>(
        0x80 |                     // start bit
        ((gain_b & 0x1F) << 2) |   // blue gain
        ((gain_g & 0x1F) >> 3));   // upper green bits
    const uint8_t byte1 = static_cast<uint8_t>(
        ((gain_g & 0x07) << 5) |   // lower green bits
        (gain_r & 0x1F));          // red gain

    const uint8_t blue_h  = static_cast<uint8_t>((blue >> 8) & 0xFF);
    const uint8_t blue_l  = static_cast<uint8_t>(blue & 0xFF);
    const uint8_t green_h = static_cast<uint8_t>((green >> 8) & 0xFF);
    const uint8_t green_l = static_cast<uint8_t>(green & 0xFF);
    const uint8_t red_h   = static_cast<uint8_t>((red >> 8) & 0xFF);
    const uint8_t red_l   = static_cast<uint8_t>(red & 0xFF);

    for (int i = 0; i < HD108_NUM_LEDS; ++i) {
        const int base = HD108_START_FRAME_BYTES + i * HD108_LED_FRAME_BYTES;
        tx_buf[base + 0] = byte0;
        tx_buf[base + 1] = byte1;

        if (i < HD108_ACTIVE_LEDS) {
            tx_buf[base + 2] = blue_h;
            tx_buf[base + 3] = blue_l;
            tx_buf[base + 4] = green_h;
            tx_buf[base + 5] = green_l;
            tx_buf[base + 6] = red_h;
            tx_buf[base + 7] = red_l;
        }
    }

    spi_transaction_t t = {};
    t.length    = HD108_TOTAL_BYTES * 8;  // bits
    t.tx_buffer = tx_buf;

    err = spi_device_transmit(s_spi_dev, &t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_device_transmit failed: %s", esp_err_to_name(err));
    }

    heap_caps_free(tx_buf);
    
    // NOTE: We deliberately do NOT free the SPI bus/device here to maintain pin state.
    // The device handle s_spi_dev remains valid for next call.

    return err;
}

static struct {
    struct arg_int *gain_b;
    struct arg_int *gain_g;
    struct arg_int *gain_r;
    struct arg_int *blue;
    struct arg_int *green;
    struct arg_int *red;
    struct arg_end *end;
} hd108_args;

static int hd108_set_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &hd108_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, hd108_args.end, argv[0]);
        return 1;
    }

    const int gain_b = hd108_args.gain_b->ival[0];
    const int gain_g = hd108_args.gain_g->ival[0];
    const int gain_r = hd108_args.gain_r->ival[0];
    const int blue   = hd108_args.blue->ival[0];
    const int green  = hd108_args.green->ival[0];
    const int red    = hd108_args.red->ival[0];

    if (gain_b < 0 || gain_b > 31 || gain_g < 0 || gain_g > 31 || gain_r < 0 || gain_r > 31) {
        printf("Gain values must be between 0 and 31\n");
        return 1;
    }
    if (blue < 0 || blue > 65535 || green < 0 || green > 65535 || red < 0 || red > 65535) {
        printf("Color values must be between 0 and 65535\n");
        return 1;
    }

    esp_err_t err = hd108_send_frame(
        static_cast<uint8_t>(gain_b),
        static_cast<uint8_t>(gain_g),
        static_cast<uint8_t>(gain_r),
        static_cast<uint16_t>(blue),
        static_cast<uint16_t>(green),
        static_cast<uint16_t>(red));

    if (err != ESP_OK) {
        printf("hd108_set failed: %s\n", esp_err_to_name(err));
        return 1;
    }

    printf("HD108 frame sent: gains(b,g,r)=(%d,%d,%d) colors(b,g,r)=(%d,%d,%d) for first %d pixels\n",
           gain_b, gain_g, gain_r, blue, green, red, HD108_ACTIVE_LEDS);
    return 0;
}

void register_hd108_debug(void)
{
    hd108_args.gain_b = arg_int1(NULL, NULL, "<gain_b>", "Blue gain (0-31)");
    hd108_args.gain_g = arg_int1(NULL, NULL, "<gain_g>", "Green gain (0-31)");
    hd108_args.gain_r = arg_int1(NULL, NULL, "<gain_r>", "Red gain (0-31)");
    hd108_args.blue   = arg_int1(NULL, NULL, "<blue16>", "Blue 16-bit value (0-65535)");
    hd108_args.green  = arg_int1(NULL, NULL, "<green16>", "Green 16-bit value (0-65535)");
    hd108_args.red    = arg_int1(NULL, NULL, "<red16>", "Red 16-bit value (0-65535)");
    hd108_args.end    = arg_end(6);

    const esp_console_cmd_t cmd = {
        .command = "hd108_set",
        .help = "Set first 100 HD108 pixels: gains + 16-bit RGB. Args: gain_b gain_g gain_r blue green red",
        .hint = NULL,
        .func = &hd108_set_cmd,
        .argtable = &hd108_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

