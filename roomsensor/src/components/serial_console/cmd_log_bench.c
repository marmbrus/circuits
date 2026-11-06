// log_bench: what one ESP_LOGx call costs the calling task, in stack and in time.
//
// Each case runs in a fresh task with an internal stack of BENCH_STACK bytes, logs BENCH_LINES
// typical lines, and reports the deepest stack it reached (from the high-water mark) and the
// average time per call. "none" logs nothing, to show the task's own overhead. INFO lines go to
// the console only (the default wifi.loglevel forwards WARN and up); WARN lines also go to MQTT
// through netlog, so this publishes BENCH_LINES lines on sensor/<mac>/logs/warn.

#include "cmd_log_bench.h"

#include "esp_console.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

#define BENCH_STACK 6144
#define BENCH_LINES 20

static const char *TAG = "log_bench";

typedef enum { BENCH_NONE, BENCH_INFO, BENCH_WARN } bench_kind_t;

typedef struct {
    bench_kind_t kind;
    TaskHandle_t caller;
    uint32_t stack_used;
    int64_t us_per_line;
} bench_run_t;

static void bench_task(void *arg)
{
    bench_run_t *run = (bench_run_t *)arg;
    int64_t start = esp_timer_get_time();
    for (int i = 0; i < BENCH_LINES; i++) {
        // A typical line: a few numbers and a string
        if (run->kind == BENCH_INFO) {
            ESP_LOGI(TAG, "line %d of %d: %s took %lld us, %u bytes", i, BENCH_LINES, "sample",
                     (long long)(esp_timer_get_time() - start), (unsigned)(i * 37));
        } else if (run->kind == BENCH_WARN) {
            ESP_LOGW(TAG, "line %d of %d: %s took %lld us, %u bytes", i, BENCH_LINES, "sample",
                     (long long)(esp_timer_get_time() - start), (unsigned)(i * 37));
        }
    }
    run->us_per_line = (esp_timer_get_time() - start) / BENCH_LINES;
    run->stack_used = BENCH_STACK - uxTaskGetStackHighWaterMark(NULL);
    xTaskNotifyGive(run->caller);
    vTaskDelete(NULL);
}

static int bench_one(bench_kind_t kind, const char *name)
{
    bench_run_t run = {.kind = kind, .caller = xTaskGetCurrentTaskHandle()};
    if (xTaskCreate(bench_task, "log_bench", BENCH_STACK, &run, 5, NULL) != pdPASS) {
        printf("log_bench: can't start the %s task\n", name);
        return 1;
    }
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Let the console drain before the next case, so cases don't wait on each other's output
    vTaskDelay(pdMS_TO_TICKS(500));
    printf("LOG BENCH %-4s stack %4u bytes, %6lld us per line\n", name, (unsigned)run.stack_used,
           (long long)run.us_per_line);
    return 0;
}

static int do_log_bench(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int err = bench_one(BENCH_NONE, "none");
    err |= bench_one(BENCH_INFO, "info");
    err |= bench_one(BENCH_WARN, "warn");
    return err;
}

void register_log_bench(void)
{
    const esp_console_cmd_t cmd = {
        .command = "log_bench",
        .help = "Measure the stack and time one log call costs its task (INFO: console; WARN: console and MQTT)",
        .hint = NULL,
        .func = &do_log_bench,
        .argtable = NULL,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
