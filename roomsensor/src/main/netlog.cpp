#include "netlog.h"

#include "esp_log.h"
#include "driver/uart.h"
#include "sdkconfig.h"
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
#include "esp_heap_caps.h"
#include "freertos/idf_additions.h"

// Small log queue. Each item stores level, tag and a message string.
// We keep messages reasonably small to avoid memory pressure.
typedef struct {
    esp_log_level_t level;
    char tag[16];
    char msg[192];
} netlog_item_t;

static QueueHandle_t s_log_queue = nullptr;
static TaskHandle_t s_netlog_task = nullptr;
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
    }
}

// ESP_LOGx formats begin with the line's level letter ("I (%lu) %s: ...", after an optional colour
// code), so the level is known before anything is formatted. Returns false for anything else (a
// plain printf through esp_log_write, a library's own format), whose level the caller then reads
// from the formatted line.
static bool level_from_format(const char* fmt, esp_log_level_t* level) {
    const char* p = fmt;
    while (*p == '\x1b') {
        const char* m = strchr(p, 'm');
        if (!m) return false;
        p = m + 1;
    }
    if (p[0] == '\0' || p[1] != ' ') return false;
    switch (p[0]) {
        case 'E': *level = ESP_LOG_ERROR; return true;
        case 'W': *level = ESP_LOG_WARN; return true;
        case 'I': *level = ESP_LOG_INFO; return true;
        case 'D': *level = ESP_LOG_DEBUG; return true;
        case 'V': *level = ESP_LOG_VERBOSE; return true;
        default: return false;
    }
}

// Writes a formatted line to the serial console. Once the console's UART driver is up, the line
// goes to it in one piece: through stdio it would arrive one character at a time, and the driver
// stores each write as its own ring-buffer item (about 30 bytes per character), so its transmit
// buffer would fill within a line and the caller would wait for the UART after all. Newlines
// become \r\n, as the console's stdio does.
static void write_console(const char* text, size_t len) {
    const uart_port_t port = (uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM;
    if (!uart_is_driver_installed(port)) {
        fwrite(text, 1, len, stdout);
        return;
    }
    const char* p = text;
    const char* end = text + len;
    while (p < end) {
        const char* nl = (const char*)memchr(p, '\n', (size_t)(end - p));
        if (nl == nullptr) {
            uart_write_bytes(port, p, (size_t)(end - p));
            break;
        }
        if (nl > p && nl[-1] == '\r') {
            uart_write_bytes(port, p, (size_t)(nl + 1 - p));
        } else {
            if (nl > p) uart_write_bytes(port, p, (size_t)(nl - p));
            uart_write_bytes(port, "\r\n", 2);
        }
        p = nl + 1;
    }
}

// Queues a formatted line for MQTT. Kept out of line so its queue item (about 210 bytes) is on the
// stack only for the lines that get this far, never while a line is being formatted.
static void __attribute__((noinline)) forward_line(const char* line, size_t len) {
    netlog_item_t item = {};
    parse_tag_level(line, item.tag, sizeof(item.tag), &item.level);
    // Trim trailing newlines to keep the MQTT payload tidy
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
    size_t copy_len = len < sizeof(item.msg) - 1 ? len : sizeof(item.msg) - 1;
    memcpy(item.msg, line, copy_len);
    item.msg[copy_len] = '\0';
    (void)xQueueSend(s_log_queue, &item, 0);
}

// Every log line comes through here, on the stack of the task that logs, so it formats each line
// once: into a buffer that goes to the console (written as text, which skips the 1 KB buffer
// newlib's vprintf puts on the stack for the unbuffered console, and doesn't wait for the UART;
// see write_console) and, for lines at or above wifi.loglevel, to MQTT. That costs the caller
// about one vsnprintf instead of two printf passes.
static int netlog_vprintf_hook(const char* fmt, va_list args) {
    char line[256];
    va_list format_args;
    va_copy(format_args, args);
    int n = vsnprintf(line, sizeof(line), fmt, format_args);
    va_end(format_args);
    if (n < 0) return n;

    size_t len = (size_t)n;
    if (len < sizeof(line)) {
        // Lines end in \n; make it \r\n here so the console gets each line in one write
        size_t console_len = len;
        if (len > 0 && line[len - 1] == '\n' && (len < 2 || line[len - 2] != '\r') &&
            len + 1 < sizeof(line)) {
            line[len - 1] = '\r';
            line[len] = '\n';
            line[len + 1] = '\0';
            console_len = len + 1;
        }
        write_console(line, console_len);
    } else {
        // Too long for the buffer (a JSON dump, say): print it in full the old way. MQTT gets the
        // start of it either way.
        len = sizeof(line) - 1;
        if (s_prev_vprintf) {
            va_list print_args;
            va_copy(print_args, args);
            s_prev_vprintf(fmt, print_args);
            va_end(print_args);
        }
    }

    // Lines the netlog task logs itself (publishing can log, on failure say) stay off MQTT, or a
    // failing publish would feed itself. Every other task's lines go through, even while netlog
    // is publishing: a global "publishing" flag here once dropped them all during each publish.
    if (!s_hook_enabled || !s_log_queue || xPortInIsrContext() ||
        xTaskGetCurrentTaskHandle() == s_netlog_task) {
        return n;
    }

    esp_log_level_t level;
    if (!level_from_format(fmt, &level)) {
        char tag[16];
        parse_tag_level(line, tag, sizeof(tag), &level);
    }
    int net_level = config::GetConfigurationManager().wifi().loglevel();
    if ((int)level > net_level) return n;

    if (uxQueueSpacesAvailable(s_log_queue) == 0) {
        s_dropped_logs++;
        return n;
    }
    forward_line(line, len);
    return n;
}

esp_err_t netlog_init_early(void) {
    if (s_log_queue == nullptr) {
        // 64 items of ~210 bytes: keep them in PSRAM, as internal RAM is short (Bluetooth, audio DMA)
        s_log_queue = xQueueCreateWithCaps(64, sizeof(netlog_item_t), MALLOC_CAP_SPIRAM);
        if (!s_log_queue) s_log_queue = xQueueCreate(64, sizeof(netlog_item_t));
        if (!s_log_queue) {
            return ESP_ERR_NO_MEM;
        }
    }

    if (s_netlog_task == nullptr) {
        // Stack in PSRAM to spare internal RAM; the task never touches flash, which would need internal
        BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(&netlog_task, "netlog", 4096, nullptr, tskIDLE_PRIORITY + 1,
                                                        &s_netlog_task, tskNO_AFFINITY, MALLOC_CAP_SPIRAM);
        if (ok != pdPASS) {
            ok = xTaskCreatePinnedToCore(&netlog_task, "netlog", 4096, nullptr, tskIDLE_PRIORITY + 1, &s_netlog_task, tskNO_AFFINITY);
        }
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


