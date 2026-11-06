#pragma once

// Mic audio for MQTT: 44.1 kHz in, IMA-ADPCM blocks out, at about 7.4 KB/s.
//
// - mic_decimator: a low-pass FIR that keeps one sample in MIC_CODEC_DECIMATION, taking the mic's
//   44.1 kHz to 14.7 kHz (audio up to about 6 kHz: speech and room sound).
// - ima_adpcm: the standard 4-bit IMA ADPCM codec (the one WAV files use).
// - Blocks: what one MQTT message carries. Each starts with the encoder's state, so a listener
//   decodes any block on its own and a dropped block costs only its own audio. Layout, all
//   little-endian:
//     0  u8   version (MIC_BLOCK_VERSION)
//     1  u8   codec (MIC_BLOCK_CODEC_IMA_ADPCM)
//     2  u16  sample rate (Hz)
//     4  u32  sequence number (consecutive; a gap means blocks were dropped)
//     8  u16  samples in the block
//     10 i16  predictor before the first sample
//     12 u8   step index before the first sample
//     13 u8   reserved (0)
//     14      ADPCM codes, two per byte, the earlier sample in the low nibble
//
// Plain C with no ESP-IDF dependencies, so host_test/ runs it on the build machine.
// util/roomsensor_util/mic.py decodes it.

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIC_CODEC_DECIMATION 3
#define MIC_CODEC_TAPS 63

typedef struct {
    int32_t coefficients[MIC_CODEC_TAPS];  // Q15, summing to 1.0
    int16_t history[MIC_CODEC_TAPS];
    int position;  // where the next input goes in history
    int phase;     // inputs since the last output
} mic_decimator_t;

// Sets up a low-pass for MIC_CODEC_DECIMATION-fold decimation of audio at input_rate
void mic_decimator_init(mic_decimator_t *d, int input_rate);
// Clears the filter's history, keeping its coefficients
void mic_decimator_reset(mic_decimator_t *d);
// Takes one input sample; returns 1 and sets *out once per MIC_CODEC_DECIMATION inputs
int mic_decimator_push(mic_decimator_t *d, int16_t in, int16_t *out);

typedef struct {
    int16_t predictor;
    uint8_t step_index;  // 0..88
} ima_adpcm_state_t;

// Encodes one sample as a 4-bit code, advancing the state
uint8_t ima_adpcm_encode(ima_adpcm_state_t *state, int16_t sample);
// Decodes one 4-bit code, advancing the state
int16_t ima_adpcm_decode(ima_adpcm_state_t *state, uint8_t code);

#define MIC_BLOCK_VERSION 1
#define MIC_BLOCK_CODEC_IMA_ADPCM 1
#define MIC_BLOCK_HEADER_BYTES 14
#define MIC_BLOCK_BYTES(samples) (MIC_BLOCK_HEADER_BYTES + ((samples) + 1) / 2)

// Builds blocks of a fixed number of samples
typedef struct {
    uint8_t *buffer;  // MIC_BLOCK_BYTES(samples_per_block) bytes, owned by the caller
    uint16_t samples_per_block;
    uint16_t sample_rate;
    uint32_t sequence;  // of the block being built
    uint16_t count;     // samples in it so far
    ima_adpcm_state_t state;
} mic_block_writer_t;

void mic_block_writer_init(mic_block_writer_t *w, uint8_t *buffer, uint16_t samples_per_block,
                           uint16_t sample_rate);
// Starts over: empty block, fresh encoder. The sequence number carries on.
void mic_block_writer_reset(mic_block_writer_t *w);
// Adds a sample; returns the finished block's size in bytes when this sample completes it (the
// block is then in buffer until the next call), else 0
size_t mic_block_writer_push(mic_block_writer_t *w, int16_t sample);

#ifdef __cplusplus
}
#endif
