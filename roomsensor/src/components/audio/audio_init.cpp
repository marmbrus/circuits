#include "audio_init.h"
#include "esp_log.h"
#include "SpeakerConfig.h"
#include "ConfigurationManager.h"

#include "audio_pipeline.h"
#include "audio_element.h"
#include "audio_common.h"
#include "audio_event_iface.h"
#include "http_stream.h"
#include "mp3_decoder.h"
#include "raw_stream.h"

#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include <math.h>

static const char *TAG = "audio";

static audio_pipeline_handle_t s_pipeline = NULL;
static audio_element_handle_t s_http_stream = NULL;
static audio_element_handle_t s_mp3_decoder = NULL;
static audio_element_handle_t s_raw_stream = NULL;
static audio_event_iface_handle_t s_evt = NULL;
static i2s_chan_handle_t s_tx_chan = NULL;
static TaskHandle_t s_pump_task = NULL;
static TaskHandle_t s_evt_task = NULL;
static TaskHandle_t s_sine_task = NULL;
static TaskHandle_t s_ctrl_task = NULL;
static volatile bool s_mp3_eos_request = false;

enum class AudioMode { None, MP3, Sine };
static AudioMode s_mode = AudioMode::None;
static std::string s_last_url;
static int s_last_sine_hz = 0;
static bool s_pipeline_running = false;

static esp_err_t http_ev_cb(http_stream_event_msg_t *msg) {
	if (msg && msg->event_id == HTTP_STREAM_PRE_REQUEST && msg->http_client) {
		esp_http_client_handle_t c = (esp_http_client_handle_t)msg->http_client;
		esp_http_client_set_header(c, "Icy-MetaData", "0");
	}
	return ESP_OK;
}

static void pump_raw_to_i2s_task(void *arg) {
	const size_t buf_bytes = 2048;
	uint8_t *buf = (uint8_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_8BIT);
	ESP_LOGI(TAG, "pump: started");
	if (!buf) {
		ESP_LOGE(TAG, "raw buffer alloc failed");
		vTaskDelete(NULL);
		return;
	}
	size_t total_in = 0, total_out = 0;
	TickType_t next_log = xTaskGetTickCount() + pdMS_TO_TICKS(10000);
	while (true) {
		int bytes = raw_stream_read(s_raw_stream, (char *)buf, (int)buf_bytes);
		if (bytes > 0) {
			size_t written = 0;
			esp_err_t wr = i2s_channel_write(s_tx_chan, buf, (size_t)bytes, &written, portMAX_DELAY);
			total_in += (size_t)bytes;
			total_out += written;
			if (wr != ESP_OK || written == 0) {
				ESP_LOGW(TAG, "I2S write err=%s written=%u req=%u", esp_err_to_name(wr), (unsigned)written, (unsigned)bytes);
			}
			if (xTaskGetTickCount() >= next_log) {
				ESP_LOGI(TAG, "pump: in=%u out=%u (bytes)", (unsigned)total_in, (unsigned)total_out);
				next_log = xTaskGetTickCount() + pdMS_TO_TICKS(10000);
			}
			continue;
		} else {
			if (bytes < 0) {
				ESP_LOGW(TAG, "pump: raw read returned %d", bytes);
			}
		}
		// Handle end-of-stream and abort cases to gracefully stop the pipeline
		if (bytes == 0 || bytes == AEL_IO_DONE || bytes == AEL_IO_ABORT) {
			ESP_LOGI(TAG, "End of stream detected (bytes=%d). Requesting MP3 stop.", bytes);
			s_mp3_eos_request = true;
			break;
		}
		// Yield briefly on other transient conditions
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	free(buf);

	// Controller handles pipeline lifecycle; just exit
	s_pump_task = NULL;
	vTaskDelete(NULL);
}

static void pipeline_event_task(void *arg) {
	audio_event_iface_msg_t msg;
	while (true) {
		if (audio_event_iface_listen(s_evt, &msg, portMAX_DELAY) != ESP_OK) {
			ESP_LOGW(TAG, "evt: listen error");
			continue;
		}
		const char *src_tag = NULL;
		if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source) {
			src_tag = audio_element_get_tag((audio_element_handle_t)msg.source);
		}
		ESP_LOGI(TAG, "evt: src=%p tag=%s type=%d cmd=%d data=%p len=%d",
			msg.source,
			src_tag ? src_tag : "(null)",
			(int)msg.source_type,
			(int)msg.cmd,
			msg.data,
			(int)msg.data_len);
		if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source) {
			audio_element_state_t el_state = audio_element_get_state((audio_element_handle_t)msg.source);
			ESP_LOGI(TAG, "evt: element_state=%d", (int)el_state);
		}
		if (msg.cmd == AEL_MSG_CMD_REPORT_STATUS) {
			ESP_LOGI(TAG, "evt: report_status value=%d", (int)(intptr_t)msg.data);
		}
		if (msg.cmd == AEL_MSG_CMD_STOP) {
			ESP_LOGI(TAG, "evt: stop requested by %s", src_tag ? src_tag : "(unknown)");
		}
		if (msg.cmd == AEL_MSG_CMD_FINISH) {
			ESP_LOGI(TAG, "evt: finish reported by %s", src_tag ? src_tag : "(unknown)");
		}
		if (msg.source == (void *)s_http_stream) {
			ESP_LOGI(TAG, "evt: http_stream event cmd=%d", (int)msg.cmd);
		}
		if (msg.source == (void *)s_raw_stream) {
			ESP_LOGI(TAG, "evt: raw_stream event cmd=%d", (int)msg.cmd);
		}
		if (msg.source == (void *)s_mp3_decoder && msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
			audio_element_info_t info = {};
			audio_element_getinfo(s_mp3_decoder, &info);
			ESP_LOGI(TAG, "MP3 info: rate=%d bits=%d ch=%d", (int)info.sample_rates, (int)info.bits, (int)info.channels);
			// Re-tune I2S clock for new stream format
			i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)info.sample_rates);
			// IDF 5.3 API supports reconfiguring std clock
			i2s_channel_disable(s_tx_chan);
			i2s_channel_reconfig_std_clock(s_tx_chan, &clk);
			i2s_channel_enable(s_tx_chan);
			ESP_LOGI(TAG, "I2S clock reconfigured to %d Hz", (int)info.sample_rates);
		}
	}
}

static void stop_mp3_locked() {
	if (!s_pipeline || !s_pipeline_running) return;
	ESP_LOGI(TAG, "Stopping MP3 pipeline");
	audio_pipeline_stop(s_pipeline);
	audio_pipeline_wait_for_stop(s_pipeline);
	audio_pipeline_terminate(s_pipeline);
	// Reset elements to INIT state for next run
	audio_element_reset_state(s_http_stream);
	audio_element_reset_state(s_mp3_decoder);
	audio_element_reset_state(s_raw_stream);
	audio_pipeline_reset_ringbuffer(s_pipeline);
	if (s_pump_task) { vTaskDelete(s_pump_task); s_pump_task = NULL; }
	if (s_evt_task) { vTaskDelete(s_evt_task); s_evt_task = NULL; }
	s_pipeline_running = false;
}

static void start_mp3_locked(const char* url) {
	ESP_LOGI(TAG, "Starting MP3: %s", url);
	// Ensure I2S is enabled and elements are reset before reuse
	i2s_channel_disable(s_tx_chan);
	i2s_channel_enable(s_tx_chan);
	audio_element_reset_state(s_http_stream);
	audio_element_reset_state(s_mp3_decoder);
	audio_element_reset_state(s_raw_stream);
	audio_element_set_uri(s_http_stream, url);
	esp_err_t r = audio_pipeline_run(s_pipeline);
	if (r != ESP_OK) {
		ESP_LOGE(TAG, "audio_pipeline_run failed: %s", esp_err_to_name(r));
		s_pipeline_running = false;
		return;
	}
	if (!s_pump_task) {
		BaseType_t ok = xTaskCreate(pump_raw_to_i2s_task, "pump_raw_to_i2s", 4096, NULL, 5, &s_pump_task);
		ESP_LOGI(TAG, "pump task create %s", ok == pdPASS ? "ok" : "FAILED");
	}
	if (!s_evt_task) {
		BaseType_t ok = xTaskCreate(pipeline_event_task, "audio_evt", 4096, NULL, 5, &s_evt_task);
		ESP_LOGI(TAG, "evt task create %s", ok == pdPASS ? "ok" : "FAILED");
	}
	s_pipeline_running = true;
}

static void sine_task(void *arg) {
	int freq_hz = (int)(intptr_t)arg;
	ESP_LOGI(TAG, "Sine start: %d Hz", freq_hz);
	i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(44100);
	i2s_channel_disable(s_tx_chan);
	i2s_channel_reconfig_std_clock(s_tx_chan, &clk);
	i2s_channel_enable(s_tx_chan);
	const size_t frames = 512;
	int16_t *samples = (int16_t*)heap_caps_malloc(frames * 2 * sizeof(int16_t), MALLOC_CAP_8BIT);
	if (!samples) { ESP_LOGE(TAG, "sine buffer alloc failed"); vTaskDelete(NULL); return; }
	float phase = 0.0f;
	float step = 2.0f * (float)M_PI * (float)freq_hz / 44100.0f;
	const float amplitude = 32767.0f * 0.1f;
	while (true) {
		for (size_t i = 0; i < frames; ++i) {
			float s = sinf(phase) * amplitude;
			int16_t v = (int16_t)s;
			samples[i * 2 + 0] = v;
			samples[i * 2 + 1] = v;
			phase += step;
			if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
		}
		size_t written = 0;
		esp_err_t wr = i2s_channel_write(s_tx_chan, samples, frames * 2 * sizeof(int16_t), &written, portMAX_DELAY);
		static uint32_t loops = 0;
		if ((++loops % 400) == 0) {
			ESP_LOGI(TAG, "sine: wrote=%u err=%s", (unsigned)written, esp_err_to_name(wr));
		}
	}
}

static void stop_sine_locked() {
	if (s_sine_task) { vTaskDelete(s_sine_task); s_sine_task = NULL; }
}

static void start_sine_locked(int hz) {
	if (hz <= 0) hz = 1000;
	ESP_LOGI(TAG, "Switching to sine %d Hz", hz);
	xTaskCreate(sine_task, "sine_out", 4096, (void*)(intptr_t)hz, 5, &s_sine_task);
}

static void audio_control_task(void *arg) {
	while (true) {
		auto &cfg = config::GetConfigurationManager().speaker();
		// Handle end-of-song cleanup request
		if (s_mp3_eos_request && s_pipeline_running) {
			ESP_LOGI(TAG, "Control: handling end-of-stream cleanup");
			stop_mp3_locked();
			s_mp3_eos_request = false;
		}
		bool want_sine = cfg.has_sine();
		bool want_url = (!want_sine) && cfg.has_url();
		if (want_sine) {
			int hz = cfg.sine_hz();
			if (s_mode != AudioMode::Sine || hz != s_last_sine_hz) {
				stop_mp3_locked();
				stop_sine_locked();
				start_sine_locked(hz);
				s_mode = AudioMode::Sine;
				s_last_sine_hz = hz;
			}
		} else if (want_url) {
			const std::string &url = cfg.url();
			if (s_mode != AudioMode::MP3 || url != s_last_url) {
				stop_sine_locked();
				stop_mp3_locked();
				start_mp3_locked(url.c_str());
				s_mode = AudioMode::MP3;
				s_last_url = url;
			}
		} else {
			if (s_mode != AudioMode::None) {
				stop_sine_locked();
				stop_mp3_locked();
				s_mode = AudioMode::None;
			}
		}
		vTaskDelay(pdMS_TO_TICKS(500));
	}
}

void audio_component_init(void) {
	using namespace config;
	auto &cfg = GetConfigurationManager().speaker();
	if (!(cfg.has_sdin() && cfg.has_sclk() && cfg.has_lrclk())) {
		ESP_LOGW(TAG, "speaker pins not configured; skipping audio pipeline init");
		return;
	}

	esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_DEBUG);
	esp_log_level_set("AUDIO_PIPELINE", ESP_LOG_DEBUG);
	esp_log_level_set("I2S_STREAM", ESP_LOG_DEBUG);
	esp_log_level_set("MP3_DECODER", ESP_LOG_DEBUG);
	esp_log_level_set("HTTP_STREAM", ESP_LOG_DEBUG);
	esp_log_level_set("I2S", ESP_LOG_DEBUG);          // IDF driver

	// IDF I2S STD TX setup (44100 Hz, 16-bit, stereo)
	i2s_chan_config_t chan_cfg = {
		.id = I2S_NUM_0,
		.role = I2S_ROLE_MASTER,
		.dma_desc_num = 4,
		.dma_frame_num = 240,
		.auto_clear = true,
	};
	ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL));
	i2s_std_config_t std_cfg = {};
	std_cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100);
	std_cfg.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
	std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
	std_cfg.gpio_cfg.bclk = (gpio_num_t)cfg.sclk();
	std_cfg.gpio_cfg.ws   = (gpio_num_t)cfg.lrclk();
	std_cfg.gpio_cfg.dout = (gpio_num_t)cfg.sdin();
	std_cfg.gpio_cfg.din  = I2S_GPIO_UNUSED;
	std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
	std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
	std_cfg.gpio_cfg.invert_flags.ws_inv   = false;
	ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &std_cfg));
	ESP_ERROR_CHECK(i2s_channel_enable(s_tx_chan));
	ESP_LOGI(TAG, "I2S ready on SDIN=%u SCLK=%u LRCLK=%u", (unsigned)cfg.sdin(), (unsigned)cfg.sclk(), (unsigned)cfg.lrclk());

	// Create pipeline
	audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
	s_pipeline = audio_pipeline_init(&pipeline_cfg);
	if (!s_pipeline) {
		ESP_LOGE(TAG, "audio_pipeline_init failed");
		return;
	}

	// HTTP reader element (HTTPS with global certificate bundle)
	http_stream_cfg_t http_cfg = HTTP_STREAM_CFG_DEFAULT();
	http_cfg.type = AUDIO_STREAM_READER;
	http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
	http_cfg.event_handle = http_ev_cb;
	s_http_stream = http_stream_init(&http_cfg);
	if (!s_http_stream) {
		ESP_LOGE(TAG, "http_stream_init failed");
		return;
	}

	// MP3 decoder element
	mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
	s_mp3_decoder = mp3_decoder_init(&mp3_cfg);
	if (!s_mp3_decoder) {
		ESP_LOGE(TAG, "mp3_decoder_init failed");
		return;
	}

	// RAW writer element
	raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
	raw_cfg.type = AUDIO_STREAM_WRITER;
	s_raw_stream = raw_stream_init(&raw_cfg);
	if (!s_raw_stream) {
		ESP_LOGE(TAG, "raw_stream_init failed");
		return;
	}

	// Register and link
	audio_pipeline_register(s_pipeline, s_http_stream, "http");
	audio_pipeline_register(s_pipeline, s_mp3_decoder, "mp3");
	audio_pipeline_register(s_pipeline, s_raw_stream, "raw");
	const char *link_tag[3] = {"http", "mp3", "raw"};
	ESP_ERROR_CHECK(audio_pipeline_link(s_pipeline, link_tag, 3));

	// Set up event listener for pipeline
	audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
	s_evt = audio_event_iface_init(&evt_cfg);
	audio_pipeline_set_listener(s_pipeline, s_evt);

	// Defer starting playback; controller responds to config changes
	if (!s_ctrl_task) xTaskCreate(audio_control_task, "audio_ctrl", 4096, NULL, 5, &s_ctrl_task);
}
