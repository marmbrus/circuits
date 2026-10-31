#include <stdio.h>
#include <string.h>
#include <string>
#include "esp_console.h"
#include "cJSON.h"
#include "ConfigurationManager.h"
#include "cmd_config.h"

// Print the full configuration, or one module of it, as JSON
static int print_config(const char* module_name)
{
    cJSON* root = config::GetConfigurationManager().build_full_config_json();
    cJSON* node = module_name ? cJSON_GetObjectItem(root, module_name) : root;
    if (!node) {
        printf("Unknown module: %s\n", module_name);
        cJSON_Delete(root);
        return 1;
    }
    char* json = cJSON_PrintUnformatted(node);
    if (json) {
        printf("%s\n", json);
        cJSON_free(json);
    }
    cJSON_Delete(root);
    return 0;
}

// config                          -> print full configuration
// config <module>                 -> print one module
// config <module> <key> <value…>  -> apply update (same path as MQTT; persisted keys are saved)
static int config_cmd(int argc, char** argv)
{
    if (argc == 1) return print_config(nullptr);
    if (argc == 2) return print_config(argv[1]);
    if (argc == 3) {
        printf("Usage: config [<module> [<key> <value>]]\n");
        return 1;
    }

    // Values may contain spaces (e.g. led1 message); rejoin the remaining args
    std::string value = argv[3];
    for (int i = 4; i < argc; ++i) { value += " "; value += argv[i]; }

    esp_err_t err = config::GetConfigurationManager().handle_update(argv[1], argv[2], value.c_str(), true);
    // ESP_ERR_INVALID_STATE comes from publishing the new config while MQTT is down; the update still applied
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        printf("config %s.%s failed: %s\n", argv[1], argv[2], esp_err_to_name(err));
        return 1;
    }
    return print_config(argv[1]);
}

void register_config(void)
{
    const esp_console_cmd_t cmd = {
        .command = "config",
        .help = "Show or update configuration. Usage: config [<module> [<key> <value>]]\n"
                "  e.g. config led1 pattern SOLID; config led1 R 30",
        .hint = NULL,
        .func = &config_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
