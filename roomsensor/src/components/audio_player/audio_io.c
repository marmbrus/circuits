#include "audio_io.h"
#include "mic_codec.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"

static const char *TAG = "audio_io";

_Static_assert(AUDIO_IO_MIC_SAMPLE_RATE == AUDIO_IO_SAMPLE_RATE / MIC_CODEC_DECIMATION,
               "audio_io.h's mic rate must match mic_codec's decimation");

// Bytes of 16-bit stereo input the output element converts per write
#define OUTPUT_BUFFER_BYTES   2048
// Mic frames read from I2S per loop (one frame = left + right 32-bit slots)
#define MIC_READ_FRAMES       256

static i2s_chan_handle_t s_tx = NULL;
static i2s_chan_handle_t s_rx = NULL;

// Output gain in Q15 (32768 == unity)
static atomic_int s_gain_q15 = 32768 / 4;

// Where finished mic blocks go while streaming (audio_io_set_mic_sink)
static audio_io_mic_sink_t s_mic_sink = NULL;
static void *s_mic_sink_ctx = NULL;
static atomic_bool s_mic_streaming = false;

static void mic_task(void *arg);

esp_err_t audio_io_init(const audio_io_pins_t *pins)
{
    if (!pins || pins->bclk_gpio < 0 || pins->ws_gpio < 0) return ESP_ERR_INVALID_ARG;
    if (s_tx || s_rx) return ESP_ERR_INVALID_STATE;

    bool want_tx = pins->dout_gpio >= 0;
    bool want_rx = pins->din_gpio >= 0;
    if (!want_tx && !want_rx) return ESP_ERR_INVALID_ARG;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // Send silence instead of repeating stale DMA buffers when nothing is playing
    chan_cfg.auto_clear = true;
    esp_err_t err = i2s_new_channel(&chan_cfg, want_tx ? &s_tx : NULL, want_rx ? &s_rx : NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_IO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = pins->bclk_gpio,
            .ws = pins->ws_gpio,
            .dout = want_tx ? pins->dout_gpio : I2S_GPIO_UNUSED,
            .din = want_rx ? pins->din_gpio : I2S_GPIO_UNUSED,
            .invert_flags = { 0 },
        },
    };
    // Return errors rather than abort: the DMA buffers need internal RAM, and a board short of it
    // should come up without audio instead of rebooting over and over
    if (s_tx) {
        err = i2s_channel_init_std_mode(s_tx, &std_cfg);
        if (err == ESP_OK) err = i2s_channel_enable(s_tx);
    }
    if (err == ESP_OK && s_rx) {
        err = i2s_channel_init_std_mode(s_rx, &std_cfg);
        if (err == ESP_OK) err = i2s_channel_enable(s_rx);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S setup failed: %s", esp_err_to_name(err));
        if (s_tx) {
            i2s_channel_disable(s_tx);
            i2s_del_channel(s_tx);
            s_tx = NULL;
        }
        if (s_rx) {
            i2s_channel_disable(s_rx);
            i2s_del_channel(s_rx);
            s_rx = NULL;
        }
        return err;
    }

    if (s_rx) {
        // The mic task's stack can live in PSRAM (it only reads I2S and encodes into static
        // buffers), sparing internal RAM for DMA
        BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(mic_task, "audio-mic", 4096, NULL, 6, NULL, 1, MALLOC_CAP_SPIRAM);
        if (ok != pdPASS) ok = xTaskCreatePinnedToCore(mic_task, "audio-mic", 4096, NULL, 6, NULL, 1);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to start mic task");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "I2S ready at %d Hz (speaker %s, mic %s)", AUDIO_IO_SAMPLE_RATE,
             s_tx ? "on" : "off", s_rx ? "on" : "off");
    return ESP_OK;
}

void audio_io_set_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    // Squared curve: perceived loudness tracks the setting better than linear gain
    atomic_store(&s_gain_q15, (int)((32768LL * volume * volume) / 10000));
}

// ---------------------------------------------------------------------------
// Speaker output element

static int output_write(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    static int32_t out[OUTPUT_BUFFER_BYTES / sizeof(int16_t)];
    if (!s_tx || len <= 0) return len;

    const int16_t *in = (const int16_t *)buffer;
    int total_samples = len / (int)sizeof(int16_t);
    int gain = atomic_load(&s_gain_q15);
    int done = 0;
    while (done < total_samples) {
        int n = total_samples - done;
        if (n > (int)(sizeof(out) / sizeof(out[0]))) n = sizeof(out) / sizeof(out[0]);
        for (int i = 0; i < n; i++) {
            // 16-bit sample, scaled, placed in the top of the 32-bit slot
            out[i] = (int32_t)in[done + i] * gain * 2;
        }
        size_t written = 0;
        esp_err_t err = i2s_channel_write(s_tx, out, n * sizeof(int32_t), &written, ticks_to_wait);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) return AEL_IO_FAIL;
        done += written / sizeof(int32_t);
        if (written < n * sizeof(int32_t)) break;
    }
    return done * sizeof(int16_t);
}

// Nothing to open (I2S runs from boot), but ADF only moves an element to its running state
// through its open callback: without one the element's task spins without ever calling process.
static esp_err_t output_open(audio_element_handle_t self)
{
    (void)self;
    return ESP_OK;
}

static int output_process(audio_element_handle_t self, char *in_buffer, int in_len)
{
    int r_size = audio_element_input(self, in_buffer, in_len);
    if (r_size == AEL_IO_TIMEOUT) {
        // Upstream underrun: keep the I2S clock fed with silence
        memset(in_buffer, 0, in_len);
        r_size = in_len;
    }
    if (r_size <= 0) return r_size;
    int w_size = audio_element_output(self, in_buffer, r_size);
    if (w_size > 0) audio_element_update_byte_pos(self, w_size);
    return w_size;
}

audio_element_handle_t audio_io_create_output_element(void)
{
    if (!s_tx) {
        ESP_LOGE(TAG, "No speaker output configured");
        return NULL;
    }
    audio_element_cfg_t cfg = DEFAULT_AUDIO_ELEMENT_CONFIG();
    cfg.open = output_open;
    cfg.process = output_process;
    cfg.write = output_write;
    cfg.buffer_len = OUTPUT_BUFFER_BYTES;
    cfg.tag = "speaker";
    // ADF's default 2 KB overflowed as soon as the element logged (every log line goes through
    // netlog's hook, which formats it again). The task only writes to I2S, never to flash, so its
    // stack can live in PSRAM like the other pipeline elements'.
    cfg.task_stack = 4096;
    cfg.stack_in_ext = true;
    audio_element_handle_t el = audio_element_init(&cfg);
    if (el) audio_element_set_music_info(el, AUDIO_IO_SAMPLE_RATE, 2, 16);
    return el;
}

// ---------------------------------------------------------------------------
// Microphone

static void mic_task(void *arg)
{
    static int32_t frames[MIC_READ_FRAMES * 2];
    static mic_decimator_t decimator;
    static uint8_t block[MIC_BLOCK_BYTES(AUDIO_IO_MIC_BLOCK_SAMPLES)];
    static mic_block_writer_t writer;
    mic_decimator_init(&decimator, AUDIO_IO_SAMPLE_RATE);
    mic_block_writer_init(&writer, block, AUDIO_IO_MIC_BLOCK_SAMPLES, AUDIO_IO_MIC_SAMPLE_RATE);
    bool was_streaming = false;

    for (;;) {
        // Keep reading even when not streaming, so the RX DMA never backs up
        size_t bytes_read = 0;
        if (i2s_channel_read(s_rx, frames, sizeof(frames), &bytes_read, portMAX_DELAY) != ESP_OK) continue;
        bool streaming = atomic_load(&s_mic_streaming) && s_mic_sink != NULL;
        if (!streaming) {
            was_streaming = false;
            continue;
        }
        if (!was_streaming) {
            // Each stream starts clean: no stale filter history, a fresh encoder
            mic_decimator_reset(&decimator);
            mic_block_writer_reset(&writer);
            was_streaming = true;
        }
        int n = bytes_read / (2 * sizeof(int32_t));
        for (int i = 0; i < n; i++) {
            // The ICS-43434's L/R pin is tied low, so it drives the left slot with
            // 24-bit samples left-justified in 32 bits; keep the top 16 bits
            int16_t sample = (int16_t)(frames[2 * i] >> 16);
            int16_t decimated;
            if (!mic_decimator_push(&decimator, sample, &decimated)) continue;
            size_t size = mic_block_writer_push(&writer, decimated);
            if (size) s_mic_sink(block, size, s_mic_sink_ctx);
        }
    }
}

bool audio_io_has_mic(void)
{
    return s_rx != NULL;
}

void audio_io_set_mic_sink(audio_io_mic_sink_t sink, void *ctx)
{
    s_mic_sink_ctx = ctx;
    s_mic_sink = sink;
}

void audio_io_set_mic_streaming(bool on)
{
    atomic_store(&s_mic_streaming, on);
}
