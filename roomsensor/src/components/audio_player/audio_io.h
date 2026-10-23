#pragma once

// Owns the I2S port shared by the speaker amp and the microphone.
//
// Both chips sit on one set of BCLK/WS lines, so the port runs full duplex at a
// fixed rate with 32-bit slots (the ICS-43434 mic needs 64 BCLK cycles per frame):
//   - TX: an ESP-ADF output element feeds decoded 16-bit stereo audio at
//     AUDIO_IO_SAMPLE_RATE; it is expanded to 32-bit slots and scaled by the volume.
//     With nothing playing, the channel sends silence.
//   - RX: a task reads the mic continuously and streams it to HTTP clients of
//     GET /mic.wav as 16-bit mono PCM WAV at AUDIO_IO_SAMPLE_RATE.

#include "esp_err.h"
#include "esp_http_server.h"
#include "audio_element.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_IO_SAMPLE_RATE 44100

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

// GET handler for live mic audio. Register it before any wildcard route.
esp_err_t audio_io_mic_http_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif
