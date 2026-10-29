#include "BLEScanner.h"

#include "BLEConfig.h"
#include "ConfigurationManager.h"
#include "TagsConfig.h"
#include "ble_scan_logic.h"
#include "communication.h"
#include "system_state.h"

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#if defined(CONFIG_BT_NIMBLE_ENABLED)
#include "esp_wifi.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#endif

extern const uint8_t* get_device_mac(void);

namespace ble {

static const char* TAG = "BLEScanner";

// The control task wakes this often to follow config changes and close reporting windows
static constexpr TickType_t kTickPeriod = pdMS_TO_TICKS(500);
// Distinct devices remembered per discovery window, and how many of them a report lists
static constexpr size_t kDiscoveryCapacity = 128;
static constexpr size_t kMaxReportedDevices = 64;

// Keeps the discovery table (about 10 KB when full) out of internal RAM, which Wi-Fi, the BLE
// controller and DMA need, falling back to internal RAM if PSRAM is short. Allocations under
// CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL would otherwise go to internal RAM first.
template <class T>
struct SpiramPreferredAllocator {
    using value_type = T;
    SpiramPreferredAllocator() = default;
    template <class U>
    SpiramPreferredAllocator(const SpiramPreferredAllocator<U>&) {}
    T* allocate(size_t n) {
        void* p = heap_caps_malloc_prefer(n * sizeof(T), 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_DEFAULT);
        // As operator new does without exceptions: a std::vector can't take a null
        if (p == nullptr) abort();
        return static_cast<T*>(p);
    }
    void deallocate(T* p, size_t) { heap_caps_free(p); }
};
template <class T, class U>
bool operator==(const SpiramPreferredAllocator<T>&, const SpiramPreferredAllocator<U>&) { return true; }
template <class T, class U>
bool operator!=(const SpiramPreferredAllocator<T>&, const SpiramPreferredAllocator<U>&) { return false; }

using DiscoveryTable = DeviceTable<SpiramPreferredAllocator<SeenDevice>>;

static config::ConfigurationManager* s_cfg = nullptr;
static TaskHandle_t s_task = nullptr;

// Guards everything the BLE host task writes and the control task reads
static SemaphoreHandle_t s_lock = nullptr;
static TrackedWindows s_tracked;
static DiscoveryTable s_devices(kDiscoveryCapacity);
static bool s_discovery = false;

static void format_mac_nosep_lower(char* out, size_t out_len) {
    const uint8_t* mac = get_device_mac();
    snprintf(out, out_len, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Current UTC time as ISO 8601 with milliseconds: YYYY-MM-DDTHH:MM:SS.mmmZ
static void format_iso8601_utc(char* out, size_t out_len) {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    struct tm tm_utc;
    time_t secs = tv.tv_sec;
    gmtime_r(&secs, &tm_utc);
    char base[24];
    strftime(base, sizeof(base), "%Y-%m-%dT%H:%M:%S", &tm_utc);
    snprintf(out, out_len, "%s.%03dZ", base, (int)(tv.tv_usec / 1000));
}

static double round_tenth(float v) {
    return roundf(v * 10.0f) / 10.0f;
}

// The board tags every metric reading carries (see create_tag_collection in i2c/metrics_tags.cpp)
static void add_board_tags(cJSON* tags) {
    const config::TagsConfig& t = s_cfg->tags();
    cJSON_AddStringToObject(tags, "area", t.area().c_str());
    cJSON_AddStringToObject(tags, "room", t.room().c_str());
    cJSON_AddStringToObject(tags, "id", t.id().c_str());
    const uint8_t* mac = get_device_mac();
    char mac_upper[13];
    snprintf(mac_upper, sizeof(mac_upper), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(tags, "mac", mac_upper);
    cJSON_AddStringToObject(tags, "sensor", (t.room() + "-" + t.id()).c_str());
}

static void publish_json(const char* topic, cJSON* root) {
    char* json = cJSON_PrintUnformatted(root);
    if (json) {
        publish_to_topic(topic, json, 0, 0);
        cJSON_free(json);
    }
}

// sensor/<mac>/metrics/ble_rssi for one tracked device heard during the window
static void publish_tracked(const TrackedWindows::Report& r, double window_sec) {
    char mac[13];
    format_mac_nosep_lower(mac, sizeof(mac));
    char topic[64];
    snprintf(topic, sizeof(topic), "sensor/%s/metrics/ble_rssi", mac);
    char ts[32];
    format_iso8601_utc(ts, sizeof(ts));

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "metric", "ble_rssi");
    cJSON_AddNumberToObject(root, "value", round_tenth(r.rssi.mean()));
    cJSON_AddStringToObject(root, "ts", ts);
    cJSON* tags = cJSON_AddObjectToObject(root, "tags");
    add_board_tags(tags);
    cJSON_AddStringToObject(tags, "name", r.device.name.c_str());
    cJSON_AddStringToObject(tags, "ble_addr", format_address(r.device.addr).c_str());
    cJSON_AddStringToObject(tags, "type", "ble");
    // Beyond the metric format: the window's spread, for anything that wants more than the mean
    cJSON_AddNumberToObject(root, "min", r.rssi.min);
    cJSON_AddNumberToObject(root, "max", r.rssi.max);
    cJSON_AddNumberToObject(root, "count", r.rssi.count);
    cJSON_AddNumberToObject(root, "window_s", round_tenth((float)window_sec));
    if (r.adv.has_tx_power) cJSON_AddNumberToObject(root, "tx_power", r.adv.tx_power);
    publish_json(topic, root);
    cJSON_Delete(root);
}

// sensor/<mac>/ble/devices: every device heard during the window, strongest first
static void publish_discovery(const DiscoveryTable::Entries& entries, size_t dropped,
                              const config::BLEConfig::Settings& s, double window_sec) {
    char mac[13];
    format_mac_nosep_lower(mac, sizeof(mac));
    char topic[64];
    snprintf(topic, sizeof(topic), "sensor/%s/ble/devices", mac);
    char ts[32];
    format_iso8601_utc(ts, sizeof(ts));

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ts", ts);
    cJSON_AddNumberToObject(root, "window_s", round_tenth((float)window_sec));
    cJSON_AddNumberToObject(root, "heard", (double)(entries.size() + dropped));
    if (s.has_min_rssi) cJSON_AddNumberToObject(root, "min_rssi", s.min_rssi);
    cJSON* devices = cJSON_AddArrayToObject(root, "devices");

    size_t listed = 0;
    for (const SeenDevice& e : entries) {
        if (s.has_min_rssi && e.rssi.max < s.min_rssi) continue;
        if (listed >= kMaxReportedDevices) break;
        ++listed;

        cJSON* d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "addr", format_address(e.addr).c_str());
        cJSON_AddStringToObject(d, "addr_type", describe_address_type(e.addr_type, e.addr));
        cJSON_AddNumberToObject(d, "rssi", round_tenth(e.rssi.mean()));
        cJSON_AddNumberToObject(d, "min", e.rssi.min);
        cJSON_AddNumberToObject(d, "max", e.rssi.max);
        cJSON_AddNumberToObject(d, "count", e.rssi.count);
        if (e.adv.name[0] != '\0') cJSON_AddStringToObject(d, "name", e.adv.name);
        if (e.adv.uuid_count > 0) {
            cJSON* uuids = cJSON_AddArrayToObject(d, "uuids");
            for (uint8_t i = 0; i < e.adv.uuid_count; ++i) {
                char uuid[5];
                snprintf(uuid, sizeof(uuid), "%04x", e.adv.uuids16[i]);
                cJSON_AddItemToArray(uuids, cJSON_CreateString(uuid));
            }
        }
        if (e.adv.has_manufacturer) {
            char mfr[5];
            snprintf(mfr, sizeof(mfr), "%04x", e.adv.manufacturer_id);
            cJSON_AddStringToObject(d, "mfr", mfr);
        }
        if (e.adv.has_tx_power) cJSON_AddNumberToObject(d, "tx_power", e.adv.tx_power);
        if (is_tile(e.adv)) cJSON_AddBoolToObject(d, "tile", true);
        for (const TrackedDevice& t : s.tracked) {
            if (t.addr == e.addr) {
                cJSON_AddStringToObject(d, "tracked", t.name.c_str());
                break;
            }
        }
        cJSON_AddItemToArray(devices, d);
    }
    cJSON_AddNumberToObject(root, "listed", (double)listed);
    publish_json(topic, root);
    cJSON_Delete(root);
}

#if defined(CONFIG_BT_NIMBLE_ENABLED)

// Scan timing, in 0.625 ms units: listen for duty% of every 100 ms
static constexpr uint16_t kScanIntervalUnits = 160;
static constexpr uint16_t kMinScanWindowUnits = 4;
// After a scan fails to start, wait this long before trying again
static constexpr int64_t kScanRetryUs = 10 * 1000000LL;

static bool s_nimble_started = false;
static bool s_nimble_failed = false;
static volatile bool s_synced = false;
static uint8_t s_own_addr_type = 0;
// What the running scan was started with
static bool s_scan_active = false;
static int s_scan_duty = 0;
static int64_t s_next_scan_attempt_us = 0;

static int on_gap_event(struct ble_gap_event* event, void* arg) {
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            const struct ble_gap_disc_desc& d = event->disc;
            Address addr = address_from_le(d.addr.val);
            Advertisement adv;
            parse_advertisement(d.data, d.length_data, adv);
            // Never stall the host task; a report missed under contention is one sample of many
            if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
                s_tracked.record(addr, d.rssi, adv);
                if (s_discovery) s_devices.record(addr, d.addr.type, d.rssi, adv);
                xSemaphoreGive(s_lock);
            }
            return 0;
        }
        case BLE_GAP_EVENT_DISC_COMPLETE:
            // The control task notices the scan stopped and restarts it if it's still wanted
            ESP_LOGW(TAG, "Scan stopped (reason %d)", event->disc_complete.reason);
            return 0;
        default:
            return 0;
    }
}

static void on_sync(void) {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "No usable Bluetooth address (rc=%d)", rc);
        return;
    }
    ESP_LOGI(TAG, "Bluetooth host synced");
    s_synced = true;
}

static void on_reset(int reason) {
    s_synced = false;
    ESP_LOGW(TAG, "Bluetooth host reset (reason %d)", reason);
}

static void host_task(void* param) {
    (void)param;
    // Returns only if nimble_port_stop() is called
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static bool start_nimble() {
    if (s_nimble_started) return true;
    if (s_nimble_failed) return false;

    // Wi-Fi and Bluetooth share the radio, which needs Wi-Fi modem sleep (IDF aborts otherwise)
    esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (ps_err != ESP_OK) {
        ESP_LOGW(TAG, "Could not set Wi-Fi modem sleep: %s", esp_err_to_name(ps_err));
    }

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        // Most likely out of memory; don't retry every tick, only after a reboot
        ESP_LOGE(TAG, "Bluetooth init failed: %s", esp_err_to_name(err));
        s_nimble_failed = true;
        return false;
    }
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    nimble_port_freertos_init(host_task);
    s_nimble_started = true;
    ESP_LOGI(TAG, "Bluetooth started");
    return true;
}

static void start_scan(bool active, int duty_percent) {
    struct ble_gap_disc_params params;
    memset(&params, 0, sizeof(params));
    params.itvl = kScanIntervalUnits;
    uint32_t window = (uint32_t)kScanIntervalUnits * (uint32_t)duty_percent / 100;
    if (window < kMinScanWindowUnits) window = kMinScanWindowUnits;
    if (window > kScanIntervalUnits) window = kScanIntervalUnits;
    params.window = (uint16_t)window;
    params.filter_policy = 0;
    params.limited = 0;
    // Discovery asks for scan responses, which carry most devices' names; tracking only listens
    params.passive = active ? 0 : 1;
    // Every advertisement is a fresh RSSI sample
    params.filter_duplicates = 0;

    int rc = ble_gap_disc(s_own_addr_type, BLE_HS_FOREVER, &params, on_gap_event, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start scan (rc=%d); retrying in %lld s", rc, kScanRetryUs / 1000000LL);
        s_next_scan_attempt_us = esp_timer_get_time() + kScanRetryUs;
        return;
    }
    s_scan_active = active;
    s_scan_duty = duty_percent;
    ESP_LOGI(TAG, "Scanning (%s, %d%% duty)", active ? "active" : "passive", duty_percent);
}

// Brings Bluetooth and the scan in line with the settings
static void reconcile_scan(const config::BLEConfig::Settings& s) {
    bool want = s.enabled();
    if (want && !start_nimble()) return;
    if (!s_nimble_started || !s_synced) return;

    bool running = ble_gap_disc_active() != 0;
    if (running && (!want || s.scan != s_scan_active || s.duty_percent != s_scan_duty)) {
        ble_gap_disc_cancel();
        running = false;
        if (!want) ESP_LOGI(TAG, "Scanning stopped");
    }
    if (want && !running && esp_timer_get_time() >= s_next_scan_attempt_us) start_scan(s.scan, s.duty_percent);
}

#else  // !CONFIG_BT_NIMBLE_ENABLED

static void reconcile_scan(const config::BLEConfig::Settings& s) {
    static bool warned = false;
    if (s.enabled() && !warned) {
        ESP_LOGE(TAG, "ble is configured, but this firmware was built without Bluetooth (CONFIG_BT_NIMBLE_ENABLED)");
        warned = true;
    }
}

#endif  // CONFIG_BT_NIMBLE_ENABLED

static void control_task(void* arg) {
    (void)arg;
    config::BLEConfig& cfg = s_cfg->ble();
    config::BLEConfig::Settings settings;
    uint32_t seen_generation = 0;
    bool first = true;
    int64_t window_start_us = esp_timer_get_time();

    for (;;) {
        uint32_t generation = cfg.generation();
        if (first || generation != seen_generation) {
            first = false;
            seen_generation = generation;
            settings = cfg.settings();
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_tracked.set_devices(settings.tracked);
            s_discovery = settings.scan;
            if (settings.scan) {
                s_devices.take();
            } else {
                s_devices.release();
            }
            xSemaphoreGive(s_lock);
            window_start_us = esp_timer_get_time();
            if (settings.enabled()) {
                ESP_LOGI(TAG, "Config: discovery %s, %u tracked, every %d s, %d%% duty",
                         settings.scan ? "on" : "off", (unsigned)settings.tracked.size(),
                         settings.interval_sec, settings.duty_percent);
            }
        }

        reconcile_scan(settings);

        int64_t now_us = esp_timer_get_time();
        int64_t window_us = (int64_t)settings.interval_sec * 1000000LL;
        if (settings.enabled() && now_us - window_start_us >= window_us) {
            double window_sec = (double)(now_us - window_start_us) / 1e6;
            window_start_us = now_us;

            std::vector<TrackedWindows::Report> reports;
            DiscoveryTable::Entries entries;
            size_t dropped = 0;
            xSemaphoreTake(s_lock, portMAX_DELAY);
            reports = s_tracked.take();
            if (settings.scan) entries = s_devices.take(&dropped);
            xSemaphoreGive(s_lock);

            // Readings from while MQTT is down are dropped, as a stale RSSI says little
            if (get_system_state() == FULLY_CONNECTED) {
                for (const TrackedWindows::Report& r : reports) publish_tracked(r, window_sec);
                if (settings.scan) publish_discovery(entries, dropped, settings, window_sec);
            }
            ESP_LOGD(TAG, "Window %.1f s: %u tracked heard, %u devices", window_sec,
                     (unsigned)reports.size(), (unsigned)(entries.size() + dropped));
        }

        vTaskDelay(kTickPeriod);
    }
}

esp_err_t init_scanner(config::ConfigurationManager& cfg) {
    if (s_task != nullptr) return ESP_OK;
    s_cfg = &cfg;
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == nullptr) return ESP_ERR_NO_MEM;
    BaseType_t ok = xTaskCreate(control_task, "ble-ctrl", 6144, nullptr, 4, &s_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create BLE control task");
        s_task = nullptr;
        return ESP_FAIL;
    }
    return ESP_OK;
}

} // namespace ble
