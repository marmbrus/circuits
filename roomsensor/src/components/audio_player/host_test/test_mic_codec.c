// Host tests for mic_codec.c: the decimator's response, ADPCM fidelity, and the block format.
// Writes the blocks it makes and its own decode of them to files, so run.sh can check the
// Python decoder (util/roomsensor_util/mic.py) agrees sample for sample.

#include "mic_codec.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_RATE 44100
#define OUTPUT_RATE (INPUT_RATE / MIC_CODEC_DECIMATION)
#define BLOCK_SAMPLES 1470

static int failures = 0;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                   \
            fprintf(stderr, "\n");                          \
            failures++;                                     \
        }                                                   \
    } while (0)

// Decimates a sine of the given frequency and amplitude; returns the output's RMS, skipping
// the filter's start-up
static double decimated_rms(double freq, double amplitude, int16_t *out, int *out_count)
{
    mic_decimator_t d;
    mic_decimator_init(&d, INPUT_RATE);
    int n = 0;
    double sum = 0;
    int counted = 0;
    for (int i = 0; i < INPUT_RATE; i++) {
        int16_t s = (int16_t)lround(amplitude * sin(2 * M_PI * freq * i / INPUT_RATE));
        int16_t o;
        if (mic_decimator_push(&d, s, &o)) {
            if (out) out[n] = o;
            n++;
            if (n > MIC_CODEC_TAPS) {
                sum += (double)o * o;
                counted++;
            }
        }
    }
    if (out_count) *out_count = n;
    return sqrt(sum / counted);
}

static double db(double ratio) { return 20 * log10(ratio); }

static void test_decimator(void)
{
    const double amplitude = 10000;
    const double sine_rms = amplitude / sqrt(2);
    int count = 0;
    double rms_1k = decimated_rms(1000, amplitude, NULL, &count);
    CHECK(count == OUTPUT_RATE, "one second in gave %d samples, not %d", count, OUTPUT_RATE);
    CHECK(fabs(db(rms_1k / sine_rms)) < 0.2, "1 kHz changed by %.2f dB", db(rms_1k / sine_rms));
    double rms_5k = decimated_rms(5000, amplitude, NULL, NULL);
    CHECK(fabs(db(rms_5k / sine_rms)) < 1.0, "5 kHz changed by %.2f dB", db(rms_5k / sine_rms));
    // Above the output's Nyquist frequency (7.35 kHz), tones must not alias back in
    double rms_9k = decimated_rms(9000, amplitude, NULL, NULL);
    CHECK(db(rms_9k / sine_rms) < -40, "9 kHz only down %.1f dB", db(rms_9k / sine_rms));
    double rms_12k = decimated_rms(12000, amplitude, NULL, NULL);
    CHECK(db(rms_12k / sine_rms) < -60, "12 kHz only down %.1f dB", db(rms_12k / sine_rms));
}

// Encodes a 1 kHz tone into blocks, decodes them, and checks the result against the input
static void test_blocks(const char *blocks_path, const char *decoded_path)
{
    int16_t *pcm = malloc(sizeof(int16_t) * OUTPUT_RATE);
    int count = 0;
    decimated_rms(1000, 12000, pcm, &count);

    static uint8_t buffer[MIC_BLOCK_BYTES(BLOCK_SAMPLES)];
    mic_block_writer_t w;
    mic_block_writer_init(&w, buffer, BLOCK_SAMPLES, OUTPUT_RATE);
    FILE *blocks = fopen(blocks_path, "wb");
    FILE *decoded = fopen(decoded_path, "wb");
    CHECK(blocks && decoded, "can't write test output");
    if (!blocks || !decoded) return;

    int blocks_made = 0;
    double error = 0, signal = 0;
    int sample_index = 0;
    for (int i = 0; i < count; i++) {
        size_t size = mic_block_writer_push(&w, pcm[i]);
        if (size == 0) continue;
        CHECK(size == MIC_BLOCK_BYTES(BLOCK_SAMPLES), "block of %zu bytes", size);
        CHECK(buffer[0] == MIC_BLOCK_VERSION && buffer[1] == MIC_BLOCK_CODEC_IMA_ADPCM, "header");
        uint32_t seq = buffer[4] | buffer[5] << 8 | buffer[6] << 16 | (uint32_t)buffer[7] << 24;
        CHECK(seq == (uint32_t)blocks_made, "block %d has sequence %u", blocks_made, seq);
        uint16_t rate = buffer[2] | buffer[3] << 8;
        CHECK(rate == OUTPUT_RATE, "rate %u", rate);

        // Each block decodes on its own, from the state in its header
        ima_adpcm_state_t st;
        st.predictor = (int16_t)(buffer[10] | buffer[11] << 8);
        st.step_index = buffer[12];
        uint16_t n = buffer[8] | buffer[9] << 8;
        CHECK(n == BLOCK_SAMPLES, "block has %u samples", n);
        uint32_t len = (uint32_t)size;
        fwrite(&len, sizeof(len), 1, blocks);
        fwrite(buffer, 1, size, blocks);
        for (int k = 0; k < n; k++) {
            uint8_t b = buffer[MIC_BLOCK_HEADER_BYTES + k / 2];
            int16_t out = ima_adpcm_decode(&st, k % 2 ? b >> 4 : b & 15);
            fwrite(&out, sizeof(out), 1, decoded);
            int16_t in = pcm[sample_index++];
            // The encoder needs a few samples to reach the signal's step size
            if (sample_index > 64) {
                error += (double)(out - in) * (out - in);
                signal += (double)in * in;
            }
        }
        blocks_made++;
    }
    fclose(blocks);
    fclose(decoded);
    CHECK(blocks_made == count / BLOCK_SAMPLES, "made %d blocks from %d samples", blocks_made, count);
    double snr = 10 * log10(signal / error);
    CHECK(snr > 20, "ADPCM SNR only %.1f dB", snr);
    printf("decimator and ADPCM: 1 kHz in %d blocks, SNR %.1f dB\n", blocks_made, snr);
    free(pcm);
}

static void test_reset(void)
{
    static uint8_t buffer[MIC_BLOCK_BYTES(10)];
    mic_block_writer_t w;
    mic_block_writer_init(&w, buffer, 10, OUTPUT_RATE);
    for (int i = 0; i < 25; i++) mic_block_writer_push(&w, (int16_t)(i * 1000));
    mic_block_writer_reset(&w);
    size_t size = 0;
    for (int i = 0; i < 10; i++) size = mic_block_writer_push(&w, 0);
    CHECK(size == MIC_BLOCK_BYTES(10), "a reset writer fills a whole block");
    CHECK(buffer[4] == 2, "the sequence carries on across a reset (got %u)", buffer[4]);
    CHECK(buffer[10] == 0 && buffer[11] == 0 && buffer[12] == 0, "a reset starts a fresh encoder");
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <blocks out> <decoded out>\n", argv[0]);
        return 2;
    }
    test_decimator();
    test_blocks(argv[1], argv[2]);
    test_reset();
    if (failures) {
        fprintf(stderr, "mic_codec: %d failure(s)\n", failures);
        return 1;
    }
    printf("mic_codec: all tests passed\n");
    return 0;
}
