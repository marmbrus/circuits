#include "netlog.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "wifi.h"
#include "communication.h"
#include "freertos/portmacro.h"
#include "ConfigurationManager.h"
#include "WifiConfig.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Small log queue. Each item stores level, tag and a message string.
// We keep messages reasonably small to avoid memory pressure.
typedef struct {
    esp_log_level_t level;
    char tag[16];
    char msg[192];
} netlog_item_t;

static QueueHandle_t s_log_queue = nullptr;
static TaskHandle_t s_netlog_task = nullptr;
static volatile bool s_in_publish = false;
static volatile bool s_hook_enabled = true;
static volatile uint32_t s_dropped_logs = 0;

// vprintf-like hook to capture log output
static int netlog_vprintf_hook(const char* fmt, va_list args);

// Original vprintf to forward to default output
static int (*s_prev_vprintf)(const char*, va_list) = nullptr;

// Parse first token of fmt that looks like color + tag + level prefix that ESP logs use
// Example format from ESP-IDF is produced by esp_log_write:
// "I (123) TAG: message..." or similar; however vprintf receives the already formatted string.
// We will attempt a best-effort to extract tag and level at the start of the line.
static void parse_tag_level(const char* line, char* out_tag, size_t out_tag_len, esp_log_level_t* out_level) {
    // Defaults
    if (out_tag && out_tag_len) out_tag[0] = '\0';
    if (out_level) *out_level = ESP_LOG_INFO;

    if (!line) return;

    // Look for pattern: "X (..timestamp..) TAG: ..." where X in {E, W, I, D, V}
    const char* p = line;
    // Skip ANSI color codes if present
    while (*p == '\x1b') {
        const char* m = strchr(p, 'm');
        if (!m) break;
        p = m + 1;
    }
    char lvlch = *p;
    switch (lvlch) {
        case 'E': if (out_level) *out_level = ESP_LOG_ERROR; break;
        case 'W': if (out_level) *out_level = ESP_LOG_WARN; break;
        case 'I': if (out_level) *out_level = ESP_LOG_INFO; break;
        case 'D': if (out_level) *out_level = ESP_LOG_DEBUG; break;
        case 'V': if (out_level) *out_level = ESP_LOG_VERBOSE; break;
        default: break;
    }

    // Find ") TAG:"
    const char* rparen = strchr(p, ')');
    if (!rparen) return;
    const char* sp = strchr(rparen, ' ');
    if (!sp) return;
    // Skip spaces
    while (*sp == ' ') sp++;
    // Read TAG up to ':'
    const char* colon = strchr(sp, ':');
    if (!colon) return;
    size_t tag_len = (size_t)(colon - sp);
    if (tag_len >= out_tag_len) tag_len = out_tag_len - 1;
    if (out_tag && out_tag_len > 0) {
        memcpy(out_tag, sp, tag_len);
        out_tag[tag_len] = '\0';
    }
}

static const char* level_to_str(esp_log_level_t lvl) {
    switch (lvl) {
        case ESP_LOG_ERROR: return "error";
        case ESP_LOG_WARN: return "warn";
        case ESP_LOG_INFO: return "info";
        case ESP_LOG_DEBUG: return "debug";
        case ESP_LOG_VERBOSE: return "verbose";
        case ESP_LOG_NONE: default: return "none";
    }
}

static void netlog_task(void* arg) {
    (void)arg;
    netlog_item_t item;

    char mac_str[13];
    for (;;) {
        // Wait for a queued log
        if (xQueueReceive(s_log_queue, &item, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // Ensure network is connected before attempting to publish.
        if (get_system_state() != FULLY_CONNECTED) {
            // Poll-wait until connected, but also keep accepting new messages (queue caps pressure).
            do {
                vTaskDelay(pdMS_TO_TICKS(200));
            } while (get_system_state() != FULLY_CONNECTED);
        }

        // Build topic: sensor/$mac/logs/$level (include dropped count if any)
        const uint8_t* mac = get_device_mac();
        if (!mac) {
            continue;
        }
        snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        char topic[96];
        snprintf(topic, sizeof(topic), "sensor/%s/logs/%s", mac_str, level_to_str(item.level));

        // Publish raw message text. Keep QoS 0 to avoid feedback loops; no retain.
        s_in_publish = true;
        // If many logs were dropped due to backpressure, occasionally emit a summary
        uint32_t dropped = s_dropped_logs;
        if (dropped > 0) {
            char notice[128];
            int m = snprintf(notice, sizeof(notice), "[netlog] dropped %u log lines due to backpressure", (unsigned)dropped);
            if (m > 0) {
                publish_to_topic(topic, notice, 0, 0);
            }
            s_dropped_logs = 0;
        }
        publish_to_topic(topic, item.msg, 0, 0);
        s_in_publish = false;
    }
}

static int netlog_vprintf_hook(const char* fmt, va_list args) {
    // Always forward to the original sink first (keeps console behavior consistent)
    int forwarded = 0;
    if (s_prev_vprintf) {
        va_list forward_args;
        va_copy(forward_args, args);
        forwarded = s_prev_vprintf(fmt, forward_args);
        va_end(forward_args);
    }

    // Cheap fast-path exits: disabled, ISR, tiny-stack task, not connected, or no queue
    if (!s_hook_enabled) return forwarded;
    if (xPortInIsrContext()) return forwarded;
    if (!s_log_queue) return forwarded;

    // Drop immediately if queue is full
    if (uxQueueSpacesAvailable(s_log_queue) == 0) {
        s_dropped_logs++;
        return forwarded;
    }

    // Format into a small temporary buffer using a copy of args (avoid large stack)
    char buffer[160];
    va_list capture_args;
    va_copy(capture_args, args);
    int n = vsnprintf(buffer, sizeof(buffer), fmt, capture_args);
    va_end(capture_args);
    if (n < 0) n = 0;
    buffer[sizeof(buffer) - 1] = '\0';

    // Attempt to parse level and tag
    netlog_item_t item = {};
    item.level = ESP_LOG_INFO;
    parse_tag_level(buffer, item.tag, sizeof(item.tag), &item.level);

    // Apply wifi.loglevel filter for net publishing only (UART unaffected)
    int net_level = ESP_LOG_WARN;
    {
        config::ConfigurationManager& cfg = config::GetConfigurationManager();
        net_level = cfg.wifi().loglevel();
    }
    if ((int)item.level > net_level) {
        return forwarded;
    }

    // Copy message, trimming trailing newlines to keep MQTT payload tidy
    size_t len = strnlen(buffer, sizeof(buffer));
    while (len > 0 && (buffer[len - 1] == '\n' || buffer[len - 1] == '\r')) len--;
    size_t copy_len = len < sizeof(item.msg) - 1 ? len : sizeof(item.msg) - 1;
    memcpy(item.msg, buffer, copy_len);
    item.msg[copy_len] = '\0';

    // Avoid recursion when we publish logs (publishing may log internally). Non-blocking.
    if (!s_in_publish) {
        (void)xQueueSend(s_log_queue, &item, 0);
    }

    return forwarded;
}

esp_err_t netlog_init_early(void) {
    if (s_log_queue == nullptr) {
        s_log_queue = xQueueCreate(64, sizeof(netlog_item_t));
        if (!s_log_queue) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_netlog_task == nullptr) {
        BaseType_t ok = xTaskCreatePinnedToCore(&netlog_task, "netlog", 4096, nullptr, tskIDLE_PRIORITY + 1, &s_netlog_task, tskNO_AFFINITY);
        if (ok != pdPASS) {
            s_netlog_task = nullptr;
            return ESP_FAIL;
        }
    }

    // Install printf hook to capture logs
    if (!s_prev_vprintf) {
        s_prev_vprintf = esp_log_set_vprintf(&netlog_vprintf_hook);
    }

    return ESP_OK;
}


