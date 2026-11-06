#pragma once

// Owns the I2S port shared by the speaker amp and the microphone.
//
// Both chips sit on one set of BCLK/WS lines, so the port runs full duplex at a
// fixed rate with 32-bit slots (the ICS-43434 mic needs 64 BCLK cycles per frame):
//   - TX: an ESP-ADF output element feeds decoded 16-bit stereo audio at
//     AUDIO_IO_SAMPLE_RATE; it is expanded to 32-bit slots and scaled by the volume.
//     With nothing playing, the channel sends silence.
//   - RX: a task reads the mic continuously. While streaming is on it low-passes and
//     decimates it to AUDIO_IO_MIC_SAMPLE_RATE and encodes it as IMA-ADPCM blocks of
//     AUDIO_IO_MIC_BLOCK_SAMPLES (mic_codec.h), handing each to the mic sink.

#include "esp_err.h"
#include "audio_element.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_IO_SAMPLE_RATE 44100
// The mic as streamed: decimated by 3 (mic_codec.h), in blocks of 100 ms
#define AUDIO_IO_MIC_SAMPLE_RATE (AUDIO_IO_SAMPLE_RATE / 3)
#define AUDIO_IO_MIC_BLOCK_SAMPLES (AUDIO_IO_MIC_SAMPLE_RATE / 10)

typedef struct {
    int bclk_gpio;
    int ws_gpio;
    int dout_gpio; // -1 when the board has no speaker output
    int din_gpio;  // -1 when the board has no microphone
} audio_io_pins_t;

// Start the I2S port, and the mic task when din_gpio is set
esp_err_t audio_io_init(const audio_io_pins_t *pins);

// 0..100; applied to audio written by the output element
void audio_io_set_volume(int volume);

// ADF element that writes 16-bit stereo audio at AUDIO_IO_SAMPLE_RATE to the speaker.
// Use it as the last element of a pipeline; the pipeline owns and frees it.
audio_element_handle_t audio_io_create_output_element(void);

// Whether the board has a mic (audio_io_init started its task)
bool audio_io_has_mic(void);

// Receives each finished mic block (mic_codec.h's format) while streaming. Called from the mic
// task, so it must not block: copy the block and return.
typedef void (*audio_io_mic_sink_t)(const uint8_t *block, size_t size, void *ctx);
// Set before turning streaming on
void audio_io_set_mic_sink(audio_io_mic_sink_t sink, void *ctx);
// Starts or stops encoding the mic for the sink
void audio_io_set_mic_streaming(bool on);

#ifdef __cplusplus
}
#endif
