#include "AudioPlayer.h"

#include "ConfigurationManager.h"
#include "AudioConfig.h"

#include "audio_elements.h"
#include "audio_io.h"
#include "filter_resample.h"

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include <cstring>

namespace audio {

static const char* TAG = "AudioPlayer";

// Bundled sounds: file://<name> plays kSoundsDir/<name>
static constexpr const char* kFileScheme = "file://";
static constexpr const char* kSoundsDir = "/storage/sounds/";

// Maps audio.url to what the reader element opens: HTTP(S) URLs pass through, file://<name>
// becomes a path under kSoundsDir. Returns an empty string for anything else.
static std::string resolve_source(const std::string& url, bool* is_file) {
    *is_file = false;
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) return url;
    if (url.rfind(kFileScheme, 0) != 0) return "";
    std::string name = url.substr(strlen(kFileScheme));
    if (name.empty() || name.find("..") != std::string::npos) return "";
    *is_file = true;
    return kSoundsDir + name;
}

// How often the control task checks for configuration changes
static constexpr TickType_t kPollTicks = pdMS_TO_TICKS(100);

esp_err_t AudioPlayer::init(config::ConfigurationManager& cfg_manager) {
    cfg_manager_ = &cfg_manager;
    const config::AudioConfig& cfg = cfg_manager_->audio();
    if (!cfg.has_clock_pins()) {
        ESP_LOGI(TAG, "No audio configured; player disabled");
        return ESP_OK;
    }

    audio_io_pins_t pins = {
        .bclk_gpio = cfg.bclk_gpio(),
        .ws_gpio = cfg.ws_gpio(),
        .dout_gpio = cfg.dout_gpio(),
        .din_gpio = cfg.din_gpio(),
    };
    esp_err_t err = audio_io_init(&pins);
    if (err != ESP_OK) return err;
    audio_io_set_volume(cfg.volume());
    audio_io_set_filters(cfg.highpass_hz(), cfg.eq_hz(), cfg.eq_gain_db());

    if (!cfg.has_output_pins()) return ESP_OK;
    set_amp_enabled(false);

    // Stack in PSRAM: internal RAM is scarce once WiFi and I2S are up, and this task never writes flash
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(&AudioPlayer::TaskEntry, "audio-ctrl", 4096, this, 5, &task_, 1,
                                                    MALLOC_CAP_SPIRAM);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio control task");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Audio player ready");
    return ESP_OK;
}

void AudioPlayer::TaskEntry(void* arg) {
    static_cast<AudioPlayer*>(arg)->run();
}

void AudioPlayer::run() {
    const config::AudioConfig& cfg = cfg_manager_->audio();
    // Start from the current values; only later url updates trigger playback
    uint32_t seen_generation = cfg.generation();
    uint32_t seen_url_requests = cfg.url_requests();
    int volume = cfg.volume();
    int highpass_hz = cfg.highpass_hz(), eq_hz = cfg.eq_hz(), eq_gain_db = cfg.eq_gain_db();

    for (;;) {
        if (pipeline_) {
            if (!poll_pipeline_events()) stop();
        } else {
            vTaskDelay(kPollTicks);
        }

        uint32_t generation = cfg.generation();
        if (generation == seen_generation) continue;
        seen_generation = generation;

        if (cfg.volume() != volume) {
            // Takes effect immediately; playback doesn't restart
            volume = cfg.volume();
            audio_io_set_volume(volume);
        }
        if (cfg.highpass_hz() != highpass_hz || cfg.eq_hz() != eq_hz || cfg.eq_gain_db() != eq_gain_db) {
            highpass_hz = cfg.highpass_hz();
            eq_hz = cfg.eq_hz();
            eq_gain_db = cfg.eq_gain_db();
            audio_io_set_filters(highpass_hz, eq_hz, eq_gain_db);
        }

        // Only a url update (including republishing the same url) starts, restarts,
        // or stops playback
        uint32_t url_requests = cfg.url_requests();
        if (url_requests == seen_url_requests) continue;
        seen_url_requests = url_requests;
        stop();
        if (!cfg.url().empty()) start(cfg.url());
    }
}

esp_err_t AudioPlayer::start(const std::string& url) {
    bool is_file = false;
    std::string source = resolve_source(url, &is_file);
    if (source.empty()) {
        ESP_LOGE(TAG, "Unsupported audio url %s (use http(s)://... or file://<name>)", url.c_str());
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Playing %s", source.c_str());

    pipeline_ = audio_elements_create_pipeline();
    if (!pipeline_) {
        ESP_LOGE(TAG, "Failed to create audio pipeline");
        return ESP_FAIL;
    }

    reader_ = is_file ? audio_elements_create_file_reader() : audio_elements_create_http_reader();
    mp3_decoder_ = audio_elements_create_mp3_decoder();
    // The speaker runs at a fixed rate shared with the mic, so resample every stream to it
    resampler_ = audio_elements_create_resampler(AUDIO_IO_SAMPLE_RATE);
    output_ = audio_io_create_output_element();

    // Register whatever was created so stop() frees it even if a later element failed
    if (reader_) audio_pipeline_register(pipeline_, reader_, "reader");
    if (mp3_decoder_) audio_pipeline_register(pipeline_, mp3_decoder_, "mp3");
    if (resampler_) audio_pipeline_register(pipeline_, resampler_, "resample");
    if (output_) audio_pipeline_register(pipeline_, output_, "speaker");
    if (!reader_ || !mp3_decoder_ || !resampler_ || !output_) {
        ESP_LOGE(TAG, "Failed to create audio elements");
        stop();
        return ESP_FAIL;
    }

    const char* link_tag[4] = {"reader", "mp3", "resample", "speaker"};
    audio_pipeline_link(pipeline_, &link_tag[0], 4);
    audio_element_set_uri(reader_, source.c_str());

    events_ = audio_elements_create_event_iface();
    audio_pipeline_set_listener(pipeline_, events_);

    set_amp_enabled(true);
    if (audio_pipeline_run(pipeline_) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start audio pipeline");
        stop();
        return ESP_FAIL;
    }
    return ESP_OK;
}

void AudioPlayer::stop() {
    if (!pipeline_) return;

    set_amp_enabled(false);
    audio_pipeline_stop(pipeline_);
    audio_pipeline_wait_for_stop(pipeline_);
    audio_pipeline_remove_listener(pipeline_);
    // Terminates the pipeline and frees its elements
    audio_pipeline_deinit(pipeline_);
    if (events_) audio_event_iface_destroy(events_);

    pipeline_ = nullptr;
    reader_ = nullptr;
    mp3_decoder_ = nullptr;
    resampler_ = nullptr;
    output_ = nullptr;
    events_ = nullptr;
    ESP_LOGI(TAG, "Playback stopped");
}

bool AudioPlayer::poll_pipeline_events() {
    audio_event_iface_msg_t msg;
    if (audio_event_iface_listen(events_, &msg, kPollTicks) != ESP_OK) return true;
    if (msg.source_type != AUDIO_ELEMENT_TYPE_ELEMENT) return true;

    if (msg.source == (void*)mp3_decoder_ && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
        audio_element_info_t info = {};
        audio_element_getinfo(mp3_decoder_, &info);
        ESP_LOGI(TAG, "Stream info: %d Hz, %d bits, %d ch", info.sample_rates, info.bits, info.channels);
        rsp_filter_set_src_info(resampler_, info.sample_rates, info.channels);
        return true;
    }

    if (msg.cmd == AEL_MSG_CMD_REPORT_STATUS) {
        int status = (int)msg.data;
        if (msg.source == (void*)output_ &&
            (status == AEL_STATUS_STATE_STOPPED || status == AEL_STATUS_STATE_FINISHED)) {
            ESP_LOGI(TAG, "Playback finished");
            return false;
        }
        if (status == AEL_STATUS_ERROR_OPEN || status == AEL_STATUS_ERROR_INPUT ||
            status == AEL_STATUS_ERROR_PROCESS || status == AEL_STATUS_ERROR_OUTPUT) {
            ESP_LOGE(TAG, "Playback failed in %s (status %d)",
                     audio_element_get_tag((audio_element_handle_t)msg.source), status);
            return false;
        }
    }
    return true;
}

void AudioPlayer::set_amp_enabled(bool enabled) {
    int gpio = cfg_manager_->audio().enable_gpio();
    if (gpio < 0) return;
    gpio_reset_pin((gpio_num_t)gpio);
    gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)gpio, enabled ? 1 : 0);
}

} // namespace audio
