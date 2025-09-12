#ifndef CONSOLE_BUFFER_H
#define CONSOLE_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CONSOLE_DIR_OUT = 0,
    CONSOLE_DIR_IN = 1
} console_dir_t;

// Initialize SPIRAM-backed circular buffer. Safe to call multiple times.
// Returns true on success.
bool console_buffer_init(size_t capacity_bytes);

// Append data to the buffer with timestamp and direction. Thread-safe.
void console_buffer_append(const char* data, size_t len, console_dir_t dir);

// Helper: append null-terminated strings
static inline void console_buffer_append_str(const char* str, console_dir_t dir) {
    if (!str) return;
    size_t n = 0; while (str[n] != '\0') n++;
    console_buffer_append(str, n, dir);
}

// Iterate over records from oldest to newest. For each record, callback is invoked.
// If the callback returns non-zero, iteration stops early.
typedef int (*console_buffer_iter_cb)(uint64_t ts_ms, console_dir_t dir, const char* data, size_t len, void* ctx);
void console_buffer_iterate(console_buffer_iter_cb cb, void* ctx);

#ifdef __cplusplus
}
#endif

#endif // CONSOLE_BUFFER_H


