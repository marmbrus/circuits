#include "mic_codec.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int16_t clamp16(int32_t v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

void mic_decimator_init(mic_decimator_t *d, int input_rate)
{
    // Windowed-sinc low-pass (Blackman window). The output rate's Nyquist frequency is
    // input_rate / (2 * MIC_CODEC_DECIMATION); cut off a little below it so the transition band
    // ends near Nyquist and little aliases back.
    const double nyquist_out = (double)input_rate / (2.0 * MIC_CODEC_DECIMATION);
    const double cutoff = 0.85 * nyquist_out / (double)input_rate;  // cycles per input sample
    const int mid = MIC_CODEC_TAPS / 2;
    double taps[MIC_CODEC_TAPS];
    double sum = 0;
    for (int i = 0; i < MIC_CODEC_TAPS; i++) {
        int n = i - mid;
        double sinc = (n == 0) ? 2.0 * cutoff : sin(2.0 * M_PI * cutoff * n) / (M_PI * n);
        double window = 0.42 - 0.5 * cos(2.0 * M_PI * i / (MIC_CODEC_TAPS - 1)) +
                        0.08 * cos(4.0 * M_PI * i / (MIC_CODEC_TAPS - 1));
        taps[i] = sinc * window;
        sum += taps[i];
    }
    for (int i = 0; i < MIC_CODEC_TAPS; i++) {
        d->coefficients[i] = (int32_t)lround(taps[i] / sum * 32768.0);
    }
    mic_decimator_reset(d);
}

void mic_decimator_reset(mic_decimator_t *d)
{
    memset(d->history, 0, sizeof(d->history));
    d->position = 0;
    d->phase = 0;
}

int mic_decimator_push(mic_decimator_t *d, int16_t in, int16_t *out)
{
    d->history[d->position] = in;
    d->position = (d->position + 1) % MIC_CODEC_TAPS;
    if (++d->phase < MIC_CODEC_DECIMATION) return 0;
    d->phase = 0;

    // history[position] is now the oldest sample; the filter is symmetric, so order is moot
    int64_t acc = 0;
    int h = d->position;
    for (int i = 0; i < MIC_CODEC_TAPS; i++) {
        acc += (int64_t)d->coefficients[i] * d->history[h];
        if (++h == MIC_CODEC_TAPS) h = 0;
    }
    *out = clamp16((int32_t)((acc + (1 << 14)) >> 15));
    return 1;
}

static const int16_t STEP_TABLE[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,
    25,    28,    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,
    88,    97,    107,   118,   130,   143,   157,   173,   190,   209,   230,   253,   279,
    307,   337,   371,   408,   449,   494,   544,   598,   658,   724,   796,   876,   963,
    1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,
    3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static const int8_t INDEX_TABLE[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

// Applies a code to the state: shared by the encoder and decoder so they stay in lockstep
static int16_t ima_adpcm_step(ima_adpcm_state_t *state, uint8_t code)
{
    int step = STEP_TABLE[state->step_index];
    int diff = step >> 3;
    if (code & 4) diff += step;
    if (code & 2) diff += step >> 1;
    if (code & 1) diff += step >> 2;
    int32_t predictor = state->predictor + ((code & 8) ? -diff : diff);
    state->predictor = clamp16(predictor);
    int index = state->step_index + INDEX_TABLE[code & 15];
    if (index < 0) index = 0;
    if (index > 88) index = 88;
    state->step_index = (uint8_t)index;
    return state->predictor;
}

uint8_t ima_adpcm_encode(ima_adpcm_state_t *state, int16_t sample)
{
    int step = STEP_TABLE[state->step_index];
    int diff = sample - state->predictor;
    uint8_t code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    if (diff >= step) {
        code |= 4;
        diff -= step;
    }
    step >>= 1;
    if (diff >= step) {
        code |= 2;
        diff -= step;
    }
    step >>= 1;
    if (diff >= step) code |= 1;
    ima_adpcm_step(state, code);
    return code;
}

int16_t ima_adpcm_decode(ima_adpcm_state_t *state, uint8_t code)
{
    return ima_adpcm_step(state, code & 15);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

void mic_block_writer_init(mic_block_writer_t *w, uint8_t *buffer, uint16_t samples_per_block,
                           uint16_t sample_rate)
{
    w->buffer = buffer;
    w->samples_per_block = samples_per_block;
    w->sample_rate = sample_rate;
    w->sequence = 0;
    mic_block_writer_reset(w);
}

void mic_block_writer_reset(mic_block_writer_t *w)
{
    w->count = 0;
    w->state.predictor = 0;
    w->state.step_index = 0;
}

size_t mic_block_writer_push(mic_block_writer_t *w, int16_t sample)
{
    uint8_t *b = w->buffer;
    if (w->count == 0) {
        b[0] = MIC_BLOCK_VERSION;
        b[1] = MIC_BLOCK_CODEC_IMA_ADPCM;
        put16(b + 2, w->sample_rate);
        put32(b + 4, w->sequence);
        put16(b + 8, w->samples_per_block);
        put16(b + 10, (uint16_t)w->state.predictor);
        b[12] = w->state.step_index;
        b[13] = 0;
    }
    uint8_t code = ima_adpcm_encode(&w->state, sample);
    uint8_t *byte = b + MIC_BLOCK_HEADER_BYTES + w->count / 2;
    if (w->count % 2 == 0) {
        *byte = code;
    } else {
        *byte |= (uint8_t)(code << 4);
    }
    if (++w->count < w->samples_per_block) return 0;
    w->count = 0;
    w->sequence++;
    return MIC_BLOCK_BYTES(w->samples_per_block);
}
