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

static const char *TAG = "audio";

static audio_pipeline_handle_t s_pipeline = NULL;
static audio_element_handle_t s_http_stream = NULL;
static audio_element_handle_t s_mp3_decoder = NULL;
static audio_element_handle_t s_raw_stream = NULL;
static audio_event_iface_handle_t s_evt = NULL;
static i2s_chan_handle_t s_tx_chan = NULL;

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
	if (!buf) {
		ESP_LOGE(TAG, "raw buffer alloc failed");
		vTaskDelete(NULL);
		return;
	}
	while (true) {
		int bytes = raw_stream_read(s_raw_stream, (char *)buf, (int)buf_bytes);
		if (bytes > 0) {
			size_t written = 0;
			i2s_channel_write(s_tx_chan, buf, (size_t)bytes, &written, portMAX_DELAY);
			continue;
		}
		// Handle end-of-stream and abort cases to gracefully stop the pipeline
		if (bytes == 0 || bytes == AEL_IO_DONE || bytes == AEL_IO_ABORT) {
			ESP_LOGI(TAG, "End of stream detected (bytes=%d). Stopping audio pipeline.", bytes);
			break;
		}
		// Yield briefly on other transient conditions
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	free(buf);

	// Graceful teardown
	if (s_pipeline) {
		audio_pipeline_stop(s_pipeline);
		audio_pipeline_wait_for_stop(s_pipeline);
		audio_pipeline_terminate(s_pipeline);
		audio_pipeline_reset_elements(s_pipeline);
		audio_pipeline_reset_ringbuffer(s_pipeline);
	}
	if (s_tx_chan) {
		i2s_channel_disable(s_tx_chan);
	}
	ESP_LOGI(TAG, "Audio pipeline stopped.");
	vTaskDelete(NULL);
}

static void pipeline_event_task(void *arg) {
	audio_event_iface_msg_t msg;
	while (true) {
		if (audio_event_iface_listen(s_evt, &msg, portMAX_DELAY) != ESP_OK) {
			continue;
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
		}
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
	esp_log_level_set("MP3_DECODER", ESP_LOG_INFO);
	esp_log_level_set("HTTP_STREAM", ESP_LOG_INFO);
	esp_log_level_set("I2S", ESP_LOG_INFO);          // IDF driver

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

	// Set URI to stream
	//const char *url = "http://knhc-ice.streamguys1.com:8080/live";
	const char *url = "https://ia801401.us.archive.org/2/items/darude-sandstorm_202201/Darude%20-%20Sandstorm.mp3";
	
	audio_element_set_uri(s_http_stream, url);

	ESP_LOGI(TAG, "Starting HTTP->MP3->RAW pipeline (44100/16/stereo), pumping to I2S");
	ESP_ERROR_CHECK(audio_pipeline_run(s_pipeline));

	xTaskCreate(pump_raw_to_i2s_task, "pump_raw_to_i2s", 4096, NULL, 5, NULL);
	xTaskCreate(pipeline_event_task, "audio_evt", 4096, NULL, 5, NULL);
}
