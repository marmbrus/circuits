#include "cmd_sk6812.h"

#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_err.h"
#include "esp_log.h"
#include "led_strip.h"
#include "led_strip_rmt.h"
#include <cstdint>
#include <cstdio>

// Standalone SK6812 debug command.
// Creates a transient RMT-backed led_strip and sets the first N pixels
// to a provided RGBW value. Mirrors the wiring assumptions and flow from
// components/leds/LEDWireEncoderSK6812.cpp but runs independently.

static const char *TAG = "cmd_sk6812";

static constexpr uint32_t SK6812_RMT_RES_HZ = 10 * 1000 * 1000; // 10 MHz
static constexpr size_t   SK6812_MEM_BLOCK_SYMBOLS = 64;        // generous for burst
static constexpr bool     SK6812_WITH_DMA = true;
static constexpr size_t   SK6812_DEFAULT_LED_COUNT = 100;

static int sk6812_set_cmd(int argc, char **argv);

static struct {
    struct arg_int *gpio;
    struct arg_int *r;
    struct arg_int *g;
    struct arg_int *b;
    struct arg_int *w;
    struct arg_int *count; // optional, defaults to 100
    struct arg_end *end;
} sk_args;

static int sk6812_set_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &sk_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, sk_args.end, argv[0]);
        return 1;
    }

    const int gpio   = sk_args.gpio->ival[0];
    const int r      = sk_args.r->ival[0];
    const int g      = sk_args.g->ival[0];
    const int b      = sk_args.b->ival[0];
    const int w      = sk_args.w->ival[0];
    const int count_in = (sk_args.count->count > 0) ? sk_args.count->ival[0] : static_cast<int>(SK6812_DEFAULT_LED_COUNT);

    if (gpio < 0 || gpio > 48) {
        printf("Invalid GPIO %d; expected 0-48 on ESP32-S3\n", gpio);
        return 1;
    }
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 || w < 0 || w > 255) {
        printf("Color values must be between 0 and 255\n");
        return 1;
    }
    if (count_in <= 0) {
        printf("LED count must be positive\n");
        return 1;
    }

    const size_t count = static_cast<size_t>(count_in);

    led_strip_config_t led_cfg = {
        .strip_gpio_num = gpio,
        .max_leds = static_cast<uint32_t>(count),
        .led_pixel_format = LED_PIXEL_FORMAT_GRBW,
        .led_model = LED_MODEL_SK6812,
        .flags = { .invert_out = 0 },
    };

    led_strip_rmt_config_t rmt_cfg = {
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
        .rmt_channel = 0,
#else
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = SK6812_RMT_RES_HZ,
#endif
        .mem_block_symbols = SK6812_MEM_BLOCK_SYMBOLS,
        .flags = { .with_dma = static_cast<uint32_t>(SK6812_WITH_DMA) },
    };

    led_strip_handle_t strip = nullptr;
    auto err = led_strip_new_rmt_device(&led_cfg, &rmt_cfg, &strip);
    if (err != ESP_OK || strip == nullptr) {
        ESP_LOGE(TAG, "led_strip_new_rmt_device failed: %s", esp_err_to_name(err));
        return 1;
    }

    for (size_t i = 0; i < count; ++i) {
        err = led_strip_set_pixel_rgbw(strip, i,
                                       static_cast<uint8_t>(r),
                                       static_cast<uint8_t>(g),
                                       static_cast<uint8_t>(b),
                                       static_cast<uint8_t>(w));
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "led_strip_set_pixel_rgbw failed at %zu: %s", i, esp_err_to_name(err));
            led_strip_del(strip);
            return 1;
        }
    }

    err = led_strip_refresh(strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip_refresh failed: %s", esp_err_to_name(err));
        led_strip_del(strip);
        return 1;
    }

    led_strip_del(strip);
    printf("SK6812 frame sent on GPIO %d: rgbw=(%d,%d,%d,%d) for %zu pixels\n", gpio, r, g, b, w, count);
    return 0;
}

void register_sk6812_debug(void)
{
    sk_args.gpio  = arg_int1(NULL, NULL, "<gpio>", "GPIO for SK6812 data line");
    sk_args.r     = arg_int1(NULL, NULL, "<r>", "Red 0-255");
    sk_args.g     = arg_int1(NULL, NULL, "<g>", "Green 0-255");
    sk_args.b     = arg_int1(NULL, NULL, "<b>", "Blue 0-255");
    sk_args.w     = arg_int1(NULL, NULL, "<w>", "White 0-255");
    sk_args.count = arg_int0(NULL, NULL, "[count]", "LED count (default 100)");
    sk_args.end   = arg_end(6);

    const esp_console_cmd_t cmd = {
        .command = "sk6812_set",
        .help = "Set first N SK6812 pixels: gpio r g b w [count]",
        .hint = NULL,
        .func = &sk6812_set_cmd,
        .argtable = &sk_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}

