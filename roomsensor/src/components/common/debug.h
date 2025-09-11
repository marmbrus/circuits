#pragma once

#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

// Log a concise memory snapshot to aid debugging allocation failures.
// Use sparingly: call only on task creation failures or critical errors.
static inline void log_memory_snapshot(const char* tag, const char* context) {
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free_heap = esp_get_minimum_free_heap_size();
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t largest_spiram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    ESP_LOGE(tag, "Memory snapshot (%s):", context ? context : "");
    ESP_LOGE(tag, "  Free heap: %zu, Min ever: %zu", free_heap, min_free_heap);
    ESP_LOGE(tag, "  Internal: free=%zu, largest=%zu", free_internal, largest_internal);
    ESP_LOGE(tag, "  SPIRAM:   free=%zu, largest=%zu", free_spiram, largest_spiram);
}


// Intentionally trigger a crash to generate a core dump (for test purposes).
// Uses a null pointer store to provoke a StoreProhibited panic, which the
// ESP-IDF coredump component (v5.3) can capture to flash when enabled.
// WARNING: Calling this will reboot the device.
static inline void trigger_test_coredump(void) {
    ESP_LOGE("coredump-test", "Triggering intentional crash for core dump test in 1 tick...");
    // Small delay to let logs flush if possible
    for (volatile int i = 0; i < 100000; ++i) { /* spin */ }
    volatile int* p = (volatile int*)0;
    *p = 42; // cause StoreProhibited
}


