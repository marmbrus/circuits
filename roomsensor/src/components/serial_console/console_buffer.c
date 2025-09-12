#include "console_buffer.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

typedef struct {
    uint64_t ts_ms;
    uint8_t dir;      // console_dir_t
    uint16_t len;     // up to 65535 per fragment
    // followed by len bytes payload
} __attribute__((packed)) console_rec_hdr_t;

typedef struct {
    uint8_t* buf;
    size_t capacity;
    size_t head; // write pos
    size_t tail; // oldest pos
    SemaphoreHandle_t mutex;
    bool inited;
} console_ring_t;

static console_ring_t s_ring = {0};

static inline uint64_t now_ms(void) {
    return (uint64_t)(esp_timer_get_time() / 1000ULL);
}

static inline size_t wrap_add(size_t a, size_t b, size_t mod) {
    a += b;
    if (a >= mod) a -= mod;
    return a;
}

bool console_buffer_init(size_t capacity_bytes) {
    if (s_ring.inited) return true;
    if (capacity_bytes < 4096) capacity_bytes = 4096;
    // Allocate in SPIRAM if available
    uint8_t* mem = (uint8_t*)heap_caps_malloc(capacity_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mem) {
        // fallback to default heap to avoid crash, but caller requested SPIRAM; still proceed
        mem = (uint8_t*)heap_caps_malloc(capacity_bytes, MALLOC_CAP_8BIT);
        if (!mem) return false;
    }
    s_ring.buf = mem;
    s_ring.capacity = capacity_bytes;
    s_ring.head = 0;
    s_ring.tail = 0;
    s_ring.mutex = xSemaphoreCreateMutex();
    s_ring.inited = (s_ring.mutex != NULL);
    return s_ring.inited;
}

static void drop_oldest_record_no_lock(void) {
    if (s_ring.head == s_ring.tail) return; // empty
    // Read header at tail
    console_rec_hdr_t hdr;
    if (s_ring.capacity - s_ring.tail >= sizeof(hdr)) {
        memcpy(&hdr, &s_ring.buf[s_ring.tail], sizeof(hdr));
        s_ring.tail = wrap_add(s_ring.tail, sizeof(hdr) + hdr.len, s_ring.capacity);
    } else {
        // header wraps; read in two steps
        size_t part = s_ring.capacity - s_ring.tail;
        memcpy(&hdr, &s_ring.buf[s_ring.tail], part);
        memcpy(((uint8_t*)&hdr) + part, &s_ring.buf[0], sizeof(hdr) - part);
        size_t data_off = (sizeof(hdr) - part);
        s_ring.tail = data_off;
        s_ring.tail = wrap_add(s_ring.tail, hdr.len, s_ring.capacity);
    }
}

void console_buffer_append(const char* data, size_t len, console_dir_t dir) {
    if (!data || len == 0) return;
    if (!s_ring.inited && !console_buffer_init(64 * 1024)) return;

    console_rec_hdr_t hdr = {
        .ts_ms = now_ms(),
        .dir = (uint8_t)dir,
        .len = (uint16_t)((len > 0xFFFF) ? 0xFFFF : len)
    };
    size_t total = sizeof(hdr) + hdr.len;

    xSemaphoreTake(s_ring.mutex, portMAX_DELAY);

    // Ensure we have space; leave at least one byte between head and tail to differentiate empty/full
    while (true) {
        size_t free_space = (s_ring.tail > s_ring.head)
            ? (s_ring.tail - s_ring.head - 1)
            : (s_ring.capacity - s_ring.head + s_ring.tail - 1);
        if (free_space >= total) break;
        drop_oldest_record_no_lock();
        if (s_ring.head == s_ring.tail) break; // after drop loop ensures progress
    }

    // Write header possibly wrapping
    if (s_ring.capacity - s_ring.head >= sizeof(hdr)) {
        memcpy(&s_ring.buf[s_ring.head], &hdr, sizeof(hdr));
        s_ring.head = wrap_add(s_ring.head, sizeof(hdr), s_ring.capacity);
    } else {
        size_t part = s_ring.capacity - s_ring.head;
        memcpy(&s_ring.buf[s_ring.head], &hdr, part);
        memcpy(&s_ring.buf[0], ((const uint8_t*)&hdr) + part, sizeof(hdr) - part);
        s_ring.head = sizeof(hdr) - part;
    }

    // Write payload possibly wrapping
    if (s_ring.capacity - s_ring.head >= hdr.len) {
        memcpy(&s_ring.buf[s_ring.head], data, hdr.len);
        s_ring.head = wrap_add(s_ring.head, hdr.len, s_ring.capacity);
    } else {
        size_t part = s_ring.capacity - s_ring.head;
        memcpy(&s_ring.buf[s_ring.head], data, part);
        memcpy(&s_ring.buf[0], data + part, hdr.len - part);
        s_ring.head = hdr.len - part;
    }

    xSemaphoreGive(s_ring.mutex);
}

void console_buffer_iterate(console_buffer_iter_cb cb, void* ctx) {
    if (!cb) return;
    if (!s_ring.inited) return;
    xSemaphoreTake(s_ring.mutex, portMAX_DELAY);
    size_t pos = s_ring.tail;
    while (pos != s_ring.head) {
        console_rec_hdr_t hdr;
        if (s_ring.capacity - pos >= sizeof(hdr)) {
            memcpy(&hdr, &s_ring.buf[pos], sizeof(hdr));
            pos = wrap_add(pos, sizeof(hdr), s_ring.capacity);
        } else {
            size_t part = s_ring.capacity - pos;
            memcpy(&hdr, &s_ring.buf[pos], part);
            memcpy(((uint8_t*)&hdr) + part, &s_ring.buf[0], sizeof(hdr) - part);
            pos = sizeof(hdr) - part;
        }
        // Temporarily copy payload into a small stack-limited buffer chunk by chunk for callback simplicity.
        // To keep API simple, allocate a temporary buffer once per record up to hdr.len (bounded to 64 KB).
        // Use heap for this transient copy to avoid large stack.
        char* temp = (char*)heap_caps_malloc(hdr.len + 1, MALLOC_CAP_8BIT);
        if (!temp) { break; }
        if (s_ring.capacity - pos >= hdr.len) {
            memcpy(temp, &s_ring.buf[pos], hdr.len);
            pos = wrap_add(pos, hdr.len, s_ring.capacity);
        } else {
            size_t part = s_ring.capacity - pos;
            memcpy(temp, &s_ring.buf[pos], part);
            memcpy(temp + part, &s_ring.buf[0], hdr.len - part);
            pos = hdr.len - part;
        }
        temp[hdr.len] = '\0';
        int stop = cb(hdr.ts_ms, (console_dir_t)hdr.dir, temp, hdr.len, ctx);
        heap_caps_free(temp);
        if (stop) break;
    }
    xSemaphoreGive(s_ring.mutex);
}


