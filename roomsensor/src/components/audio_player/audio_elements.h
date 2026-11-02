#pragma once

// ESP-ADF's default-config macros use C designated initializers that C++ rejects,
// so the pipeline pieces are built in C.

#include "audio_element.h"
#include "audio_event_iface.h"
#include "audio_pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif

audio_pipeline_handle_t audio_elements_create_pipeline(void);
audio_event_iface_handle_t audio_elements_create_event_iface(void);
// HTTP(S) reader using the certificate bundle for TLS
audio_element_handle_t audio_elements_create_http_reader(void);
// Reader for a file on a mounted filesystem (e.g. /storage/sounds/rain.mp3)
audio_element_handle_t audio_elements_create_file_reader(void);
audio_element_handle_t audio_elements_create_mp3_decoder(void);
// Resamples decoded audio to 16-bit stereo at dest_rate
audio_element_handle_t audio_elements_create_resampler(int dest_rate);

#ifdef __cplusplus
}
#endif
