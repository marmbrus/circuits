#include "cmd_mqtt_tls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_console.h"
#include "mbedtls/base64.h"
#include "mqtt_tls.h"

// A console line holds 255 characters, so util/provision.py sends each PEM base64-encoded, a piece
// per `mqtt_tls add` line, then `mqtt_tls commit`. Every subcommand answers with a line starting
// "MQTT TLS ok" or "MQTT TLS error", which the script waits for.

enum { PART_CERT, PART_KEY, PART_CA, PART_COUNT };
static const char* const PART_NAMES[PART_COUNT] = {"cert", "key", "ca"};
// The most base64 kept for one part: a few authorities' certificates.
#define MAX_STAGED 8192

static char* s_staged[PART_COUNT];
static size_t s_staged_len[PART_COUNT];

static void clear_staged(void) {
    for (int i = 0; i < PART_COUNT; i++) {
        free(s_staged[i]);
        s_staged[i] = NULL;
        s_staged_len[i] = 0;
    }
}

static int add(const char* part_name, const char* chunk) {
    int part = -1;
    for (int i = 0; i < PART_COUNT; i++) {
        if (strcmp(part_name, PART_NAMES[i]) == 0) part = i;
    }
    if (part < 0) {
        printf("MQTT TLS error: unknown part '%s' (cert, key or ca)\n", part_name);
        return 1;
    }
    size_t len = strlen(chunk);
    if (s_staged_len[part] + len > MAX_STAGED) {
        printf("MQTT TLS error: %s is longer than %d characters\n", part_name, MAX_STAGED);
        return 1;
    }
    char* grown = realloc(s_staged[part], s_staged_len[part] + len + 1);
    if (grown == NULL) {
        printf("MQTT TLS error: out of memory\n");
        return 1;
    }
    memcpy(grown + s_staged_len[part], chunk, len + 1);
    s_staged[part] = grown;
    s_staged_len[part] += len;
    printf("MQTT TLS ok: %s has %u characters\n", part_name, (unsigned)s_staged_len[part]);
    return 0;
}

// The staged base64 of `part`, decoded and NUL-terminated, or NULL.
static char* decode(int part) {
    if (s_staged[part] == NULL) return NULL;
    size_t cap = s_staged_len[part] / 4 * 3 + 4;
    unsigned char* out = malloc(cap);
    size_t out_len = 0;
    if (out == NULL || mbedtls_base64_decode(out, cap - 1, &out_len, (const unsigned char*)s_staged[part], s_staged_len[part]) != 0) {
        free(out);
        return NULL;
    }
    out[out_len] = '\0';
    return (char*)out;
}

static int commit(void) {
    char* pems[PART_COUNT] = {0};
    int result = 0;
    for (int i = 0; i < PART_COUNT; i++) {
        pems[i] = decode(i);
        if (pems[i] == NULL) {
            printf("MQTT TLS error: %s is missing or isn't base64\n", PART_NAMES[i]);
            result = 1;
            break;
        }
    }
    if (result == 0) {
        esp_err_t err = mqtt_tls_store(pems[PART_CERT], pems[PART_KEY], pems[PART_CA]);
        if (err == ESP_OK) {
            printf("MQTT TLS ok: stored; it's used from the next boot\n");
        } else {
            printf("MQTT TLS error: not stored (%s)\n", esp_err_to_name(err));
            result = 1;
        }
    }
    for (int i = 0; i < PART_COUNT; i++) free(pems[i]);
    clear_staged();
    return result;
}

static int do_mqtt_tls(int argc, char** argv) {
    const char* sub = argc >= 2 ? argv[1] : "";
    if (argc == 2 && strcmp(sub, "status") == 0) {
        mqtt_tls_print_status();
        printf("MQTT TLS ok\n");
        return 0;
    }
    if (argc == 2 && strcmp(sub, "begin") == 0) {
        clear_staged();
        printf("MQTT TLS ok: ready\n");
        return 0;
    }
    if (argc == 4 && strcmp(sub, "add") == 0) {
        return add(argv[2], argv[3]);
    }
    if (argc == 2 && strcmp(sub, "commit") == 0) {
        return commit();
    }
    if (argc == 2 && strcmp(sub, "erase") == 0) {
        esp_err_t err = mqtt_tls_erase();
        if (err != ESP_OK) {
            printf("MQTT TLS error: %s\n", esp_err_to_name(err));
            return 1;
        }
        printf("MQTT TLS ok: erased\n");
        return 0;
    }
    printf("Usage: mqtt_tls status | begin | add <cert|key|ca> <base64> | commit | erase\n");
    return 1;
}

void register_mqtt_tls(void) {
    const esp_console_cmd_t cmd = {
        .command = "mqtt_tls",
        .help = "MQTT client certificate. status: this device's name and its certificate. "
                "begin, add <cert|key|ca> <base64 of PEM>, commit: store a new one (util/provision.py --mqtt-cert). "
                "erase: remove it.",
        .hint = NULL,
        .func = &do_mqtt_tls,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
