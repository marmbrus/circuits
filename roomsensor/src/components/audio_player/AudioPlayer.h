#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>

namespace config {
class ConfigurationManager;
}

struct audio_element;
struct audio_pipeline;
struct audio_event_iface;

namespace audio {

// Plays MP3 audio from an HTTP(S) URL on the speaker, driven by the "audio"
// configuration module. Publishing audio.url starts playback (publishing the same
// url again replays it), an empty url stops it, and audio.volume (0..100) adjusts
// the level. Also starts the shared I2S port, which streams the microphone at
// GET /mic.wav (see audio_io.h). Boards without audio pins configured do nothing.
class AudioPlayer {
public:
    AudioPlayer() = default;
    ~AudioPlayer() = default;

    // Start I2S and the control task. Returns ESP_OK without starting anything when
    // the board has no audio pins configured.
    esp_err_t init(config::ConfigurationManager& cfg_manager);

private:
    static void TaskEntry(void* arg);
    void run();

    esp_err_t start(const std::string& url);
    void stop();
    // Returns false once playback has finished or failed
    bool poll_pipeline_events();
    void set_amp_enabled(bool enabled);

    config::ConfigurationManager* cfg_manager_ = nullptr;
    TaskHandle_t task_ = nullptr;

    audio_pipeline* pipeline_ = nullptr;
    audio_element* http_reader_ = nullptr;
    audio_element* mp3_decoder_ = nullptr;
    audio_element* resampler_ = nullptr;
    audio_element* output_ = nullptr;
    audio_event_iface* events_ = nullptr;
};

} // namespace audio
