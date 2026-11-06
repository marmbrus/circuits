#include "telemetry.h"
#include "ConfigurationManager.h"
#include "TagsConfig.h"
#include "wifi.h"
#include "communication.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_app_desc.h"
#include "esp_flash.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "debug.h"
#include "esp_core_dump.h"  // ESP-IDF v5.3 coredump APIs
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include <string.h>
#include "freertos/idf_additions.h"

static const char* TAG = "telemetry";

// Format MAC as lowercase hex without separators (e.g., 28372f8115f8)
static void format_mac_nosep_lower(char* out, size_t out_len) {
    const uint8_t* device_mac = get_device_mac();
    snprintf(out, out_len, "%02x%02x%02x%02x%02x%02x",
             device_mac[0], device_mac[1], device_mac[2],
             device_mac[3], device_mac[4], device_mac[5]);
}

// The core dump found at boot, read once (see telemetry_capture_boot_info): it is written only by
// a crash, just before the reboot, so it can't change while the board runs. Reading it needs flash
// access, which the telemetry task's PSRAM stack can't do.
static struct {
    bool present = false;
    bool reason_ok = false;
    bool summary_ok = false;
    char reason[200] = {0};
    esp_core_dump_summary_t summary = {};
} s_panic;

void telemetry_capture_boot_info(void) {
    s_panic.present = esp_core_dump_image_check() == ESP_OK;
    if (!s_panic.present) return;
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) && defined(CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF)
    s_panic.reason_ok = esp_core_dump_get_panic_reason(s_panic.reason, sizeof(s_panic.reason)) == ESP_OK;
    s_panic.summary_ok = esp_core_dump_get_summary(&s_panic.summary) == ESP_OK;
#endif
    ESP_LOGW(TAG, "Core dump from the last crash found; it is reported in the boot message");
}

static void erase_core_dump_task(void* arg) {
    (void)arg;
    esp_err_t er = esp_core_dump_image_erase();
    if (er != ESP_OK) {
        ESP_LOGW(TAG, "Failed to erase core dump after reporting: %s", esp_err_to_name(er));
    }
    vTaskDelete(nullptr);
}

// Single consolidated telemetry task lifecycle
// Requirements:
// - Publish the boot message immediately when MQTT connects (once per firmware boot).
//   Do NOT wait for SNTP for boot; omit boot_ts so boot still appears if time is down.
// - Heartbeat (status) messages must only be sent with correct timestamps.
//   Therefore, wait until SNTP time sync completes before sending any heartbeats.
// - Avoid error spam when disconnected: skip heartbeat publish attempts if not connected.
// - Keep the flow simple and well-documented.
static TaskHandle_t s_telemetry_task = nullptr;
static bool s_boot_published = false;  // Guard to ensure boot publish only happens once per boot

// Human-readable reset reason string (ESP-IDF v5.3)
static const char* reset_reason_to_str(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_UNKNOWN:   return "unknown";
        case ESP_RST_POWERON:   return "power_on";
        case ESP_RST_EXT:       return "external_reset";
        case ESP_RST_SW:        return "software_reset";
        case ESP_RST_PANIC:     return "panic";
        case ESP_RST_INT_WDT:   return "interrupt_wdt";
        case ESP_RST_TASK_WDT:  return "task_wdt";
        case ESP_RST_WDT:       return "other_wdt";
        case ESP_RST_DEEPSLEEP: return "deepsleep_wakeup";
        case ESP_RST_BROWNOUT:  return "brownout";
        case ESP_RST_SDIO:      return "sdio";
        case ESP_RST_USB:       return "usb";
        default:                return "other";
    }
}

// The task closest to overflowing its stack (the least unused stack, by its high-water mark), and
// how many tasks keep their stacks in internal RAM. ESP-IDF's IPC and idle tasks are left out:
// they sit at a fixed minimum by design (ipc0 at about 440 bytes), which would hide ours. Walking every stack pauses the scheduler for
// a moment, so this is refreshed once a minute rather than every heartbeat. The console's
// `tasks` command lists every task's high-water mark.
static void add_stack_summary(cJSON* root) {
#if configUSE_TRACE_FACILITY
    static int64_t s_last_us = 0;
    static char s_min_task[configMAX_TASK_NAME_LEN] = {0};
    static uint32_t s_min_free = 0;
    static uint32_t s_internal_tasks = 0;
    int64_t now_us = esp_timer_get_time();
    if (s_last_us == 0 || now_us - s_last_us >= 60 * 1000000LL) {
        UBaseType_t capacity = uxTaskGetNumberOfTasks() + 4;
        TaskStatus_t* tasks = (TaskStatus_t*)heap_caps_malloc(capacity * sizeof(TaskStatus_t),
                                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (tasks == nullptr) return;
        UBaseType_t n = uxTaskGetSystemState(tasks, capacity, nullptr);
        s_min_free = UINT32_MAX;
        s_internal_tasks = 0;
        for (UBaseType_t i = 0; i < n; i++) {
            if (esp_ptr_internal(tasks[i].pxStackBase)) s_internal_tasks++;
            const char* name = tasks[i].pcTaskName;
            if (strncmp(name, "ipc", 3) == 0 || strncmp(name, "IDLE", 4) == 0) continue;
            // On ESP-IDF stack high-water marks are in bytes
            uint32_t free_bytes = tasks[i].usStackHighWaterMark;
            if (free_bytes < s_min_free) {
                s_min_free = free_bytes;
                strlcpy(s_min_task, name, sizeof(s_min_task));
            }
        }
        heap_caps_free(tasks);
        s_last_us = now_us;
    }
    cJSON_AddStringToObject(root, "min_stack_task", s_min_task);
    cJSON_AddNumberToObject(root, "min_stack_free_bytes", (double)s_min_free);
    cJSON_AddNumberToObject(root, "internal_stack_tasks", (double)s_internal_tasks);
#else
    (void)root;
#endif
}

static void publish_device_status_once(void) {
    // Gather cheap stats
    uint64_t uptime_ms = esp_timer_get_time() / 1000ULL;
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free_heap = esp_get_minimum_free_heap_size();
    UBaseType_t num_tasks = uxTaskGetNumberOfTasks();

    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "uptime_ms", (double)uptime_ms);
    cJSON_AddNumberToObject(root, "free_heap_bytes", (double)free_heap);
    cJSON_AddNumberToObject(root, "min_free_heap_bytes", (double)min_free_heap);
    cJSON_AddNumberToObject(root, "num_tasks", (double)num_tasks);

    // Detailed memory snapshot fields (see debug.h)
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t largest_spiram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    size_t total_internal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t total_spiram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    cJSON_AddNumberToObject(root, "free_internal_bytes", (double)free_internal);
    cJSON_AddNumberToObject(root, "free_spiram_bytes", (double)free_spiram);
    cJSON_AddNumberToObject(root, "largest_internal_bytes", (double)largest_internal);
    cJSON_AddNumberToObject(root, "largest_spiram_bytes", (double)largest_spiram);
    cJSON_AddNumberToObject(root, "total_internal_bytes", (double)total_internal);
    cJSON_AddNumberToObject(root, "total_spiram_bytes", (double)total_spiram);
    // Low-water marks since boot: internal RAM, and the part of it DMA can reach, which runs
    // out first (I2S buffers, the AES DMA buffers TLS needs, the Bluetooth controller)
    cJSON_AddNumberToObject(root, "min_free_internal_bytes", (double)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    cJSON_AddNumberToObject(root, "free_dma_bytes", (double)heap_caps_get_free_size(MALLOC_CAP_DMA));
    cJSON_AddNumberToObject(root, "min_free_dma_bytes", (double)heap_caps_get_minimum_free_size(MALLOC_CAP_DMA));
    cJSON_AddNumberToObject(root, "largest_dma_bytes", (double)heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    add_stack_summary(root);

    // Absolute UTC timestamp in ISO 8601 format
    time_t now_secs = time(nullptr);
    struct tm tm_utc;
    gmtime_r(&now_secs, &tm_utc);
    char iso_ts[32];
    strftime(iso_ts, sizeof(iso_ts), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    cJSON_AddStringToObject(root, "heartbeat_ts", iso_ts);

    char mac_nosep[13] = {0};
    format_mac_nosep_lower(mac_nosep, sizeof(mac_nosep));

    char topic[64];
    snprintf(topic, sizeof(topic), "sensor/%s/device/status", mac_nosep);

    char* json = cJSON_PrintUnformatted(root);
    publish_to_topic(topic, json, 0, 0);
    cJSON_free(json);
    cJSON_Delete(root);
}

// No separate status task; heartbeats are handled inside the single telemetry task

static void publish_device_info(void) {
    cJSON *device_json = cJSON_CreateObject();

    // MAC Address
    const uint8_t* device_mac = get_device_mac();
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             device_mac[0], device_mac[1], device_mac[2],
             device_mac[3], device_mac[4], device_mac[5]);
    cJSON_AddStringToObject(device_json, "mac", mac_str);

    // IP Address
    ip_event_got_ip_t ip_event;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_event.ip_info);
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_event.ip_info.ip));
    cJSON_AddStringToObject(device_json, "ip", ip_str);

    // Chip Info
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    const char* chip_model;
    switch (chip_info.model) {
        case CHIP_ESP32:   chip_model = "ESP32"; break;
        case CHIP_ESP32S2: chip_model = "ESP32-S2"; break;
        case CHIP_ESP32S3: chip_model = "ESP32-S3"; break;
        case CHIP_ESP32C3: chip_model = "ESP32-C3"; break;
        default:           chip_model = "Unknown"; break;
    }
    cJSON_AddStringToObject(device_json, "chip_model", chip_model);
    cJSON_AddNumberToObject(device_json, "chip_revision", chip_info.revision);
    cJSON_AddNumberToObject(device_json, "cpu_cores", chip_info.cores);
    cJSON_AddBoolToObject(device_json, "features_wifi", (chip_info.features & CHIP_FEATURE_WIFI_BGN) != 0);
    cJSON_AddBoolToObject(device_json, "features_bt", (chip_info.features & CHIP_FEATURE_BT) != 0);
    cJSON_AddBoolToObject(device_json, "features_ble", (chip_info.features & CHIP_FEATURE_BLE) != 0);

    // App Info
    const esp_app_desc_t* app_desc = esp_app_get_description();
    cJSON_AddStringToObject(device_json, "app_version", app_desc->version);
    cJSON_AddStringToObject(device_json, "app_name", app_desc->project_name);
    cJSON_AddStringToObject(device_json, "compile_time", app_desc->time);
    cJSON_AddStringToObject(device_json, "compile_date", app_desc->date);
    cJSON_AddStringToObject(device_json, "idf_version", app_desc->idf_ver);

    // System Info
    uint32_t heap_size = esp_get_free_heap_size();
    uint32_t min_heap = esp_get_minimum_free_heap_size();
    cJSON_AddNumberToObject(device_json, "free_heap_bytes", heap_size);
    cJSON_AddNumberToObject(device_json, "min_free_heap_bytes", min_heap);
    // Memory totals (internal/SPIRAM)
    size_t total_internal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    size_t total_spiram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    cJSON_AddNumberToObject(device_json, "total_internal_bytes", (double)total_internal);
    cJSON_AddNumberToObject(device_json, "total_spiram_bytes", (double)total_spiram);

    // Flash Size
    uint32_t flash_size;
    esp_flash_get_size(NULL, &flash_size);
    cJSON_AddNumberToObject(device_json, "flash_size_bytes", flash_size);

    // Tags (area/room/id) from configuration
    {
        using namespace config;
        ConfigurationManager& cfg = GetConfigurationManager();
        const TagsConfig& tags = cfg.tags();
        cJSON* tags_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(tags_obj, "area", tags.area().c_str());
        cJSON_AddStringToObject(tags_obj, "room", tags.room().c_str());
        cJSON_AddStringToObject(tags_obj, "id", tags.id().c_str());
        // Derived sensor name: room-id
        char sensor_buf[160];
        snprintf(sensor_buf, sizeof(sensor_buf), "%s-%s", tags.room().c_str(), tags.id().c_str());
        cJSON_AddStringToObject(tags_obj, "sensor", sensor_buf);
        cJSON_AddItemToObject(device_json, "tags", tags_obj);
    }

    // Reset cause string at top level (single field)
    {
        esp_reset_reason_t reason = esp_reset_reason();
        const char* reason_str = reset_reason_to_str(reason);
        cJSON_AddStringToObject(device_json, "cause", reason_str);

        // Optional panic summary if a core dump was found at boot
        if (s_panic.present) {
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) && defined(CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF)
            cJSON* panic_obj = cJSON_CreateObject();
            if (s_panic.reason_ok) {
                cJSON_AddStringToObject(panic_obj, "reason_text", s_panic.reason);
            }
            if (s_panic.summary_ok) {
                const esp_core_dump_summary_t& summary = s_panic.summary;
                cJSON_AddStringToObject(panic_obj, "task", summary.exc_task);
                cJSON_AddNumberToObject(panic_obj, "pc", (double)summary.exc_pc);
#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2)
                cJSON* bt = cJSON_CreateArray();
                uint32_t max_frames = summary.exc_bt_info.depth;
                if (max_frames > 8) max_frames = 8;
                for (uint32_t i = 0; i < max_frames; ++i) {
                    cJSON_AddItemToArray(bt, cJSON_CreateNumber((double)summary.exc_bt_info.bt[i]));
                }
                cJSON_AddItemToObject(panic_obj, "backtrace", bt);
#endif
                char sha_buf[APP_ELF_SHA256_SZ + 1] = {0};
                memcpy(sha_buf, summary.app_elf_sha256, APP_ELF_SHA256_SZ);
                sha_buf[APP_ELF_SHA256_SZ] = '\0';
                cJSON_AddStringToObject(panic_obj, "app_elf_sha256", sha_buf);
                cJSON_AddNumberToObject(panic_obj, "core_dump_version", (double)summary.core_dump_version);
            }
            if (cJSON_GetArraySize(panic_obj) > 0 || (panic_obj->child != NULL)) {
                cJSON_AddItemToObject(device_json, "panic", panic_obj);
            } else {
                cJSON_Delete(panic_obj);
            }
#endif
        }
    }

    // Intentionally omit boot_ts to allow boot publish immediately upon connect
    // (SNTP may not yet be ready). The status heartbeat includes timestamps once SNTP is ready.

    // Convert to string and publish
    char *device_string = cJSON_Print(device_json); // Keep pretty JSON as existing behavior
    // Retained boot info
    publish_to_topic("device", device_string, 1, 1);

    // If a core dump was present, erase it now to avoid re-reporting it on the next boot. Only
    // after attempting to publish. Erasing is a flash operation, which this task's PSRAM stack
    // can't do, so a short-lived task with an internal stack does it.
    if (s_panic.present) {
        if (xTaskCreate(&erase_core_dump_task, "coredump_erase", 3072, nullptr, tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
            ESP_LOGW(TAG, "Failed to start the task that erases the core dump");
        }
    }

    // Cleanup
    cJSON_free(device_string);
    cJSON_Delete(device_json);
}


esp_err_t telemetry_configure_lwt(esp_mqtt_client_config_t* mqtt_cfg) {
    if (!mqtt_cfg) return ESP_ERR_INVALID_ARG;

    char mac_nosep[13] = {0};
    format_mac_nosep_lower(mac_nosep, sizeof(mac_nosep));

    char lwt_topic[128];
    snprintf(lwt_topic, sizeof(lwt_topic), "sensor/%s/device/connected", mac_nosep);

    const char* lwt_message = "{\"connected\":false}";
    // Configure LWT using ESP-IDF v5.3 nested session.last_will fields
    mqtt_cfg->session.last_will.topic = strdup(lwt_topic);
    mqtt_cfg->session.last_will.msg = (const char*)strdup(lwt_message);
    mqtt_cfg->session.last_will.msg_len = strlen(lwt_message);
    mqtt_cfg->session.last_will.qos = 1;
    mqtt_cfg->session.last_will.retain = 1;

    ESP_LOGI(TAG, "LWT configured for topic: %s", lwt_topic);
    return ESP_OK;
}

static void telemetry_task_entry(void* arg) {
    (void)arg;
    ESP_LOGI(TAG, "telemetry task started");

    // 1) Publish boot immediately upon first connect (once per boot)
    if (!s_boot_published) {
        publish_device_info();
        s_boot_published = true;
    }

    // 2) Wait for SNTP time sync before any heartbeat messages
    //    wifi_wait_for_time_sync may return ESP_ERR_INVALID_STATE until SNTP is initialized.
    //    Keep retrying until it returns ESP_OK.
    for (;;) {
        esp_err_t time_ok = wifi_wait_for_time_sync(60000);
        if (time_ok == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "SNTP synchronized; starting heartbeats");

    // 3) Heartbeat loop every 10 seconds, only when fully connected
    const TickType_t period_ticks = pdMS_TO_TICKS(10000);
    for (;;) {
        if (get_system_state() == FULLY_CONNECTED) {
            publish_device_status_once();
        }
        vTaskDelay(period_ticks);
    }
}

void telemetry_report_connected(void) {
    // Called on MQTT connect. Start the consolidated telemetry task once per boot.
    if (s_telemetry_task == nullptr) {
        // Stack in PSRAM to spare internal RAM. The task must not touch flash (IDF asserts if a task
        // with an external stack does): the core dump is read at boot and erased by its own task.
        BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(&telemetry_task_entry, "telemetry", 4096, nullptr, tskIDLE_PRIORITY + 2,
                                                        &s_telemetry_task, tskNO_AFFINITY, MALLOC_CAP_SPIRAM);
        if (ok != pdPASS) {
            ok = xTaskCreatePinnedToCore(&telemetry_task_entry, "telemetry", 4096, nullptr, tskIDLE_PRIORITY + 2, &s_telemetry_task, tskNO_AFFINITY);
        }
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Failed to create telemetry task; publishing boot inline");
            s_telemetry_task = nullptr;
            // Fallback: publish boot inline to preserve the once-per-boot guarantee
            if (!s_boot_published) {
                publish_device_info();
                s_boot_published = true;
            }
            log_memory_snapshot(TAG, "telemetry_task_create_failed");
        }
    }
}


