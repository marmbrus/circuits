#include "audio_elements.h"

#include "esp_crt_bundle.h"
#include "http_stream.h"
#include "filter_resample.h"
#include "mp3_decoder.h"

audio_pipeline_handle_t audio_elements_create_pipeline(void)
{
    audio_pipeline_cfg_t cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    return audio_pipeline_init(&cfg);
}

audio_event_iface_handle_t audio_elements_create_event_iface(void)
{
    audio_event_iface_cfg_t cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    return audio_event_iface_init(&cfg);
}

audio_element_handle_t audio_elements_create_http_reader(void)
{
    http_stream_cfg_t cfg = HTTP_STREAM_CFG_DEFAULT();
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    return http_stream_init(&cfg);
}

audio_element_handle_t audio_elements_create_mp3_decoder(void)
{
    mp3_decoder_cfg_t cfg = DEFAULT_MP3_DECODER_CONFIG();
    return mp3_decoder_init(&cfg);
}

audio_element_handle_t audio_elements_create_resampler(int dest_rate)
{
    rsp_filter_cfg_t cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    cfg.dest_rate = dest_rate;
    cfg.dest_ch = 2;
    cfg.dest_bits = 16;
    return rsp_filter_init(&cfg);
}
