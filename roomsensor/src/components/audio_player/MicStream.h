#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace config {
class ConfigurationManager;
}

namespace audio {

// Streams the microphone over MQTT on sensor/<mac>/audio/mic while audio.mic is on: one QoS 0
// message per 100 ms of audio, IMA-ADPCM at 14.7 kHz, about 7.4 KB/s (mic_codec.h has the
// format; util/mic_listen.py plays or records it).
//
// Audio never holds up MQTT: the mic task hands blocks over through a short queue, and when the
// connection can't keep up (or is down) blocks are dropped, which listeners see as gaps in the
// sequence numbers.
class MicStream {
public:
    // Starts the publishing task on boards with a mic (after audio_io_init)
    esp_err_t init(config::ConfigurationManager& cfg);

private:
    static void Sink(const uint8_t* block, size_t size, void* ctx);
    static void TaskEntry(void* arg);
    void run();

    config::ConfigurationManager* cfg_ = nullptr;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
};

} // namespace audio
