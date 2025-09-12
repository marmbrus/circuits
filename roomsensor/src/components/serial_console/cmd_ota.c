#include "cmd_ota.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "esp_log.h"
#include "ota.h"

static const char* TAG = "cmd_ota";

static struct {
    struct arg_str* hash;
    struct arg_end* end;
} forceota_args;

static int do_force_ota(int argc, char** argv) {
    int nerrors = arg_parse(argc, argv, (void**)&forceota_args);
    if (nerrors != 0) {
        arg_print_errors(stdout, forceota_args.end, argv[0]);
        return 1;
    }

    const char* hash = NULL;
    if (forceota_args.hash->count > 0) {
        hash = forceota_args.hash->sval[0];
    }

    ESP_LOGI(TAG, "Forcing OTA%s%s", hash ? " to hash " : "", hash ? hash : "");
    esp_err_t err = ota_force_update(hash);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "force_ota failed: %s", esp_err_to_name(err));
        return 1;
    }
    return 0;
}

void register_ota(void) {
    forceota_args.hash = arg_str0(NULL, NULL, "<hash>", "optional firmware git hash");
    forceota_args.end = arg_end(2);

    const esp_console_cmd_t cmd = {
        .command = "force_ota",
        .help = "Force OTA update now. Optional <hash> to pick exact firmware; if omitted, uses manifest version regardless of dev/newer status.",
        .hint = NULL,
        .func = &do_force_ota,
        .argtable = &forceota_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}


