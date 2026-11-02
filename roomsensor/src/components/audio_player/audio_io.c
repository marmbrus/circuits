#include "audio_io.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

static const char *TAG = "audio_io";

// Bytes of 16-bit stereo input the output element converts per write
#define OUTPUT_BUFFER_BYTES   2048
// Mic frames read from I2S per loop (one frame = left + right 32-bit slots)
#define MIC_READ_FRAMES       256
#define MIC_MAX_CLIENTS       2
// Per-client buffer of 16-bit mono samples (~185 ms at 44.1 kHz)
#define MIC_CLIENT_BUFFER     16384
#define MIC_CHUNK_BYTES       2048

static i2s_chan_handle_t s_tx = NULL;
static i2s_chan_handle_t s_rx = NULL;

// Output gain in Q15 (32768 == unity)
static atomic_int s_gain_q15 = 32768 / 4;

// Biquad coefficients (normalised so a0 == 1). Double-buffered: the setter fills the
// inactive set and then publishes its index, so the output task never sees a half-written set.
typedef struct {
    bool enabled;
    float b0, b1, b2, a1, a2;
} biquad_t;

typedef struct {
    biquad_t highpass;
    biquad_t eq;
} filters_t;

static filters_t s_filters[2];
static atomic_int s_filters_active = 0;

static SemaphoreHandle_t s_clients_lock = NULL;
static StreamBufferHandle_t s_clients[MIC_MAX_CLIENTS];

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
    if (s_tx) {
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std_cfg));
        ESP_ERROR_CHECK(i2s_channel_enable(s_tx));
    }
    if (s_rx) {
        ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &std_cfg));
        ESP_ERROR_CHECK(i2s_channel_enable(s_rx));
    }

    if (s_rx) {
        s_clients_lock = xSemaphoreCreateMutex();
        if (!s_clients_lock || xTaskCreatePinnedToCore(mic_task, "audio-mic", 4096, NULL, 6, NULL, 1) != pdPASS) {
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
// Speaker tuning

// RBJ Audio EQ Cookbook high-pass with Q = 1/sqrt(2) (Butterworth)
static biquad_t make_highpass(float fc)
{
    float w0 = 2.0f * (float)M_PI * fc / AUDIO_IO_SAMPLE_RATE;
    float alpha = sinf(w0) / (2.0f * 0.70710678f);
    float c = cosf(w0), a0 = 1.0f + alpha;
    return (biquad_t){ true, (1.0f + c) / 2.0f / a0, -(1.0f + c) / a0, (1.0f + c) / 2.0f / a0,
                       -2.0f * c / a0, (1.0f - alpha) / a0 };
}

// RBJ peaking filter with a one-octave bandwidth (Q ~ 1.41)
static biquad_t make_peaking(float fc, float gain_db)
{
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * (float)M_PI * fc / AUDIO_IO_SAMPLE_RATE;
    float alpha = sinf(w0) / (2.0f * 1.41421356f);
    float c = cosf(w0), a0 = 1.0f + alpha / A;
    return (biquad_t){ true, (1.0f + alpha * A) / a0, -2.0f * c / a0, (1.0f - alpha * A) / a0,
                       -2.0f * c / a0, (1.0f - alpha / A) / a0 };
}

void audio_io_set_filters(int highpass_hz, int eq_hz, int eq_gain_db)
{
    int next = !atomic_load(&s_filters_active);
    filters_t *f = &s_filters[next];
    // Above ~0.45 of the sample rate the filters stop making sense; treat as off
    const int max_hz = AUDIO_IO_SAMPLE_RATE * 45 / 100;
    f->highpass = (biquad_t){ 0 };
    f->eq = (biquad_t){ 0 };
    if (highpass_hz > 0 && highpass_hz < max_hz) f->highpass = make_highpass((float)highpass_hz);
    if (eq_hz > 0 && eq_hz < max_hz && eq_gain_db != 0) f->eq = make_peaking((float)eq_hz, (float)eq_gain_db);
    atomic_store(&s_filters_active, next);
    ESP_LOGI(TAG, "Speaker filters: high-pass %d Hz, eq %d Hz %+d dB", highpass_hz, eq_hz, eq_gain_db);
}

// Transposed direct form II; z holds the two state values for one channel
static inline float biquad_run(const biquad_t *q, float z[2], float x)
{
    float y = q->b0 * x + z[0];
    z[0] = q->b1 * x - q->a1 * y + z[1];
    z[1] = q->b2 * x - q->a2 * y;
    return y;
}

// ---------------------------------------------------------------------------
// Speaker output element

static int output_write(audio_element_handle_t self, char *buffer, int len, TickType_t ticks_to_wait, void *context)
{
    static int32_t out[OUTPUT_BUFFER_BYTES / sizeof(int16_t)];
    if (!s_tx || len <= 0) return len;

    const int16_t *in = (const int16_t *)buffer;
    int total_samples = len / (int)sizeof(int16_t);
    // Per-channel filter state (input is interleaved stereo); only this task touches it
    static float hp_z[2][2], eq_z[2][2];
    const filters_t *f = &s_filters[atomic_load(&s_filters_active)];
    // Q15 gain, times 2^16 to move the 16-bit sample into the top of the 32-bit slot
    float scale = (float)atomic_load(&s_gain_q15) * 2.0f;
    int done = 0;
    while (done < total_samples) {
        int n = total_samples - done;
        if (n > (int)(sizeof(out) / sizeof(out[0]))) n = sizeof(out) / sizeof(out[0]);
        for (int i = 0; i < n; i++) {
            int ch = (done + i) & 1;
            float x = (float)in[done + i];
            if (f->highpass.enabled) x = biquad_run(&f->highpass, hp_z[ch], x);
            if (f->eq.enabled) x = biquad_run(&f->eq, eq_z[ch], x);
            // The eq boost can push past 16-bit full scale; clip instead of wrapping
            float y = x * scale;
            if (y > 2147483520.0f) y = 2147483520.0f;
            if (y < -2147483648.0f) y = -2147483648.0f;
            out[i] = (int32_t)y;
        }
        size_t written = 0;
        esp_err_t err = i2s_channel_write(s_tx, out, n * sizeof(int32_t), &written, ticks_to_wait);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) return AEL_IO_FAIL;
        done += written / sizeof(int32_t);
        if (written < n * sizeof(int32_t)) break;
    }
    return done * sizeof(int16_t);
}

// ADF only moves an element to RUNNING through its open callback; without one the
// element task loops on a zero command timeout and never calls process
static esp_err_t output_open(audio_element_handle_t self)
{
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
    // ADF's 2 KB default overflows in i2s_channel_write; the task never writes flash, so PSRAM is fine
    cfg.task_stack = 4096;
    cfg.stack_in_ext = true;
    cfg.tag = "speaker";
    audio_element_handle_t el = audio_element_init(&cfg);
    if (el) {
        audio_element_set_music_info(el, AUDIO_IO_SAMPLE_RATE, 2, 16);
        // ADF leaves both wait times at 0, which makes this task spin on CPU 0 and starve
        // the http/mp3 tasks. Waiting on I2S paces it at the sample rate.
        audio_element_set_input_timeout(el, pdMS_TO_TICKS(20));
        audio_element_set_output_timeout(el, pdMS_TO_TICKS(200));
    }
    return el;
}

// ---------------------------------------------------------------------------
// Microphone

static void mic_task(void *arg)
{
    static int32_t frames[MIC_READ_FRAMES * 2];
    static int16_t samples[MIC_READ_FRAMES];

    for (;;) {
        size_t bytes_read = 0;
        if (i2s_channel_read(s_rx, frames, sizeof(frames), &bytes_read, portMAX_DELAY) != ESP_OK) continue;
        int n = bytes_read / (2 * sizeof(int32_t));
        for (int i = 0; i < n; i++) {
            // The ICS-43434's L/R pin is tied low, so it drives the left slot with
            // 24-bit samples left-justified in 32 bits; keep the top 16 bits
            samples[i] = (int16_t)(frames[2 * i] >> 16);
        }

        xSemaphoreTake(s_clients_lock, portMAX_DELAY);
        for (int c = 0; c < MIC_MAX_CLIENTS; c++) {
            // Never block on a slow client; it just loses audio
            if (s_clients[c]) xStreamBufferSend(s_clients[c], samples, n * sizeof(int16_t), 0);
        }
        xSemaphoreGive(s_clients_lock);
    }
}

static int mic_client_add(StreamBufferHandle_t sb)
{
    int slot = -1;
    xSemaphoreTake(s_clients_lock, portMAX_DELAY);
    for (int c = 0; c < MIC_MAX_CLIENTS; c++) {
        if (!s_clients[c]) {
            s_clients[c] = sb;
            slot = c;
            break;
        }
    }
    xSemaphoreGive(s_clients_lock);
    return slot;
}

static void mic_client_remove(int slot)
{
    xSemaphoreTake(s_clients_lock, portMAX_DELAY);
    s_clients[slot] = NULL;
    xSemaphoreGive(s_clients_lock);
}

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = v; p[1] = v >> 8;
}

// Streaming WAV header: the RIFF and data sizes are unknown, so use the maximum
static void build_wav_header(uint8_t hdr[44])
{
    const uint16_t channels = 1, bits = 16;
    memcpy(hdr, "RIFF", 4);
    put_le32(hdr + 4, 0xFFFFFFFF);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    put_le32(hdr + 16, 16);
    put_le16(hdr + 20, 1); // PCM
    put_le16(hdr + 22, channels);
    put_le32(hdr + 24, AUDIO_IO_SAMPLE_RATE);
    put_le32(hdr + 28, AUDIO_IO_SAMPLE_RATE * channels * bits / 8);
    put_le16(hdr + 32, channels * bits / 8);
    put_le16(hdr + 34, bits);
    memcpy(hdr + 36, "data", 4);
    put_le32(hdr + 40, 0xFFFFFFFF);
}

static void mic_client_task(void *arg)
{
    httpd_req_t *req = (httpd_req_t *)arg;
    static const char *busy = "Too many microphone listeners";

    // Per-listener buffer in PSRAM; internal RAM is too scarce for 16 KB per client
    StreamBufferHandle_t sb = xStreamBufferCreateWithCaps(MIC_CLIENT_BUFFER, 1, MALLOC_CAP_SPIRAM);
    int slot = sb ? mic_client_add(sb) : -1;
    if (slot < 0) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_send(req, busy, HTTPD_RESP_USE_STRLEN);
    } else {
        ESP_LOGI(TAG, "Mic listener %d connected", slot);
        httpd_resp_set_type(req, "audio/wav");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        uint8_t hdr[44];
        build_wav_header(hdr);
        static uint8_t chunk[MIC_MAX_CLIENTS][MIC_CHUNK_BYTES];
        if (httpd_resp_send_chunk(req, (const char *)hdr, sizeof(hdr)) == ESP_OK) {
            for (;;) {
                size_t n = xStreamBufferReceive(sb, chunk[slot], MIC_CHUNK_BYTES, pdMS_TO_TICKS(1000));
                if (n == 0) continue;
                if (httpd_resp_send_chunk(req, (const char *)chunk[slot], n) != ESP_OK) break;
            }
        }
        mic_client_remove(slot);
        ESP_LOGI(TAG, "Mic listener %d disconnected", slot);
    }

    if (sb) vStreamBufferDeleteWithCaps(sb);
    httpd_req_async_handler_complete(req);
    vTaskDeleteWithCaps(NULL);
}

esp_err_t audio_io_mic_http_handler(httpd_req_t *req)
{
    if (!s_rx) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No microphone configured");
    }
    // Stream from a separate task so the web server stays responsive
    httpd_req_t *async_req = NULL;
    esp_err_t err = httpd_req_async_handler_begin(req, &async_req);
    if (err != ESP_OK) return err;
    // Stack in PSRAM, like the listener buffer; this task never writes flash
    if (xTaskCreateWithCaps(mic_client_task, "mic-http", 4096, async_req, 5, NULL, MALLOC_CAP_SPIRAM) != pdPASS) {
        httpd_req_async_handler_complete(async_req);
        return ESP_FAIL;
    }
    return ESP_OK;
}
