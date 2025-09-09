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


