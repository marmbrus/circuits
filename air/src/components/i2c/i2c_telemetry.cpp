#include "i2c_telemetry.h"
#include "communication.h"
#include "cJSON.h"
#include "esp_timer.h"
#include <time.h>
#include "system_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

extern const uint8_t* get_device_mac(void);

static void format_mac_nosep_lower(char* out, size_t out_len) {
	const uint8_t* mac = get_device_mac();
	snprintf(out, out_len, "%02x%02x%02x%02x%02x%02x",
	        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Deferred topology publisher
static TaskHandle_t s_topology_task = nullptr;
static char* s_pending_json = nullptr;
static char s_pending_topic[96] = {0};

static void i2c_topology_publisher_task(void* arg) {
	(void)arg;
	const TickType_t wait_ticks = pdMS_TO_TICKS(1000);
	for (;;) {
		if (get_system_state() == FULLY_CONNECTED && s_pending_json != nullptr && s_pending_topic[0] != '\0') {
			publish_to_topic(s_pending_topic, s_pending_json, 1, 1);
			cJSON_free(s_pending_json);
			s_pending_json = nullptr;
			s_pending_topic[0] = '\0';
			TaskHandle_t self = s_topology_task;
			s_topology_task = nullptr;
			vTaskDelete(self);
		}
		vTaskDelay(wait_ticks);
	}
}

void publish_i2c_topology(const I2CSensor* const* sensors,
                          const bool* recognized,
                          int num_sensors,
                          const uint8_t* unrecognized_addrs,
                          int num_unrecognized) {
	if (!sensors || !recognized || num_sensors <= 0) return;

	// Build JSON now
	cJSON* root = cJSON_CreateObject();

	// Timestamp in ISO 8601 UTC
	time_t now_secs = time(nullptr);
	struct tm tm_utc;
	gmtime_r(&now_secs, &tm_utc);
	char iso_ts[32];
	strftime(iso_ts, sizeof(iso_ts), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
	cJSON_AddStringToObject(root, "ts", iso_ts);

	// Sensors array
	cJSON* arr = cJSON_AddArrayToObject(root, "sensors");
	for (int i = 0; i < num_sensors; ++i) {
		if (!recognized[i]) continue;
		const I2CSensor* s = sensors[i];
		if (s == nullptr) continue;
		cJSON* obj = cJSON_CreateObject();
		char addr_str[8]; snprintf(addr_str, sizeof(addr_str), "0x%02X", s->addr());
		cJSON_AddStringToObject(obj, "addr", addr_str);
		cJSON_AddStringToObject(obj, "driver", s->name().c_str());
		int idx = s->index();
		if (idx >= 0) cJSON_AddNumberToObject(obj, "index", idx);
		std::string mod = s->config_module_name();
		if (!mod.empty()) cJSON_AddStringToObject(obj, "module", mod.c_str());
		cJSON_AddItemToArray(arr, obj);
	}

	// Unrecognized addresses array
	if (unrecognized_addrs && num_unrecognized > 0) {
		cJSON* unrec = cJSON_AddArrayToObject(root, "unrecognized");
		for (int i = 0; i < num_unrecognized; ++i) {
			char addr_str[8];
			snprintf(addr_str, sizeof(addr_str), "0x%02X", unrecognized_addrs[i]);
			cJSON_AddItemToArray(unrec, cJSON_CreateString(addr_str));
		}
	}

	char* json = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);

	char mac[13];
	format_mac_nosep_lower(mac, sizeof(mac));
	char topic[96];
	snprintf(topic, sizeof(topic), "sensor/%s/device/i2c", mac);

	if (get_system_state() == FULLY_CONNECTED) {
		publish_to_topic(topic, json, 1, 1);
		cJSON_free(json);
		return;
	}

	// Defer publish until connected
	if (s_pending_json) {
		cJSON_free(s_pending_json);
		s_pending_json = nullptr;
	}
	s_pending_json = json;
	strncpy(s_pending_topic, topic, sizeof(s_pending_topic) - 1);
	s_pending_topic[sizeof(s_pending_topic) - 1] = '\0';

	if (s_topology_task == nullptr) {
		xTaskCreate(i2c_topology_publisher_task, "i2c_topo_pub", 3072, nullptr, tskIDLE_PRIORITY + 1, &s_topology_task);
	}
}


