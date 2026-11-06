#include "MicStream.h"

#include "AudioConfig.h"
#include "ConfigurationManager.h"
#include "audio_io.h"
#include "communication.h"
#include "mic_codec.h"
#include "system_state.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"

#include <atomic>
#include <stdio.h>
#include <string.h>

extern const uint8_t* get_device_mac(void);

namespace audio {

static const char* TAG = "MicStream";

// Blocks waiting to be published: 400 ms of audio rides out a slow moment on the link
static constexpr UBaseType_t kQueueBlocks = 4;
// How often the task checks the config when no audio is flowing
static constexpr TickType_t kIdleTicks = pdMS_TO_TICKS(500);
// How often dropped blocks are logged, at most
static constexpr int64_t kDropLogIntervalUs = 60 * 1000000LL;

namespace {
struct Block {
    uint16_t size;
    uint8_t data[MIC_BLOCK_BYTES(AUDIO_IO_MIC_BLOCK_SAMPLES)];
};
std::atomic<uint32_t> s_dropped{0};
} // namespace

esp_err_t MicStream::init(config::ConfigurationManager& cfg) {
    if (!audio_io_has_mic() || task_ != nullptr) return ESP_OK;
    cfg_ = &cfg;
    // Never deleted, so the IDF 5.3 WithCaps delete bugs don't apply
    queue_ = xQueueCreateWithCaps(kQueueBlocks, sizeof(Block), MALLOC_CAP_SPIRAM);
    if (queue_ == nullptr) return ESP_ERR_NO_MEM;
    // Publishing only writes to the MQTT connection, never to flash, so the stack can be in PSRAM
    if (xTaskCreatePinnedToCoreWithCaps(&MicStream::TaskEntry, "mic-stream", 4096, this, 5, &task_,
                                        tskNO_AFFINITY, MALLOC_CAP_SPIRAM) != pdPASS) {
        task_ = nullptr;
        return ESP_FAIL;
    }
    audio_io_set_mic_sink(&MicStream::Sink, this);
    return ESP_OK;
}

void MicStream::Sink(const uint8_t* block, size_t size, void* ctx) {
    auto* self = static_cast<MicStream*>(ctx);
    // Called from the mic task only, so one staging buffer will do
    static Block staged;
    if (size > sizeof(staged.data)) return;
    staged.size = (uint16_t)size;
    memcpy(staged.data, block, size);
    if (xQueueSend(self->queue_, &staged, 0) != pdTRUE) s_dropped.fetch_add(1);
}

void MicStream::TaskEntry(void* arg) {
    static_cast<MicStream*>(arg)->run();
}

void MicStream::run() {
    static Block block;
    char topic[64];
    const uint8_t* mac = get_device_mac();
    snprintf(topic, sizeof(topic), "sensor/%02x%02x%02x%02x%02x%02x/audio/mic", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
    bool streaming = false;
    int64_t last_drop_log_us = 0;
    uint32_t dropped_logged = 0;

    for (;;) {
        bool want = cfg_->audio().mic();
        if (want != streaming) {
            streaming = want;
            audio_io_set_mic_streaming(streaming);
            if (!streaming) xQueueReset(queue_);
            ESP_LOGI(TAG, "Mic streaming %s (%s)", streaming ? "on" : "off", topic);
        }

        if (xQueueReceive(queue_, &block, kIdleTicks) == pdTRUE) {
            // QoS 0: a late block is worth less than the next one
            if (get_system_state() != FULLY_CONNECTED ||
                publish_binary(topic, block.data, block.size, 0) != ESP_OK) {
                s_dropped.fetch_add(1);
            }
        }

        uint32_t dropped = s_dropped.load();
        int64_t now = esp_timer_get_time();
        if (dropped != dropped_logged && now - last_drop_log_us >= kDropLogIntervalUs) {
            ESP_LOGW(TAG, "Dropped %u mic blocks (100 ms each) since boot", (unsigned)dropped);
            dropped_logged = dropped;
            last_drop_log_us = now;
        }
    }
}

} // namespace audio
