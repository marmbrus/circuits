#include "console.h"

#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "esp_log.h"
#include "cmd_wifi.h"
#include "cmd_nvs.h"
#include "cmd_system.h"
#include "gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG_CONSOLE = "console";

static void configure_stdio_uart(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    esp_vfs_dev_uart_use_driver(UART_NUM_0);
}

static void console_task(void *arg)
{
    configure_stdio_uart();

    esp_console_config_t console_config = {
        .max_cmdline_length = 256,
        .max_cmdline_args = 16,
        .hint_color = atoi(LOG_COLOR_CYAN),
        .hint_bold = 0,
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    linenoiseSetMultiLine(1);
    linenoiseHistorySetMaxLen(100);
    linenoiseSetCompletionCallback(&esp_console_get_completion);
    linenoiseSetHintsCallback((linenoiseHintsCallback*)&esp_console_get_hint);

    esp_console_register_help_command();

    // Register standard command sets
    register_system();
    register_nvs();
    register_wifi();
    register_gpio();

    const char* prompt = LOG_COLOR_I "esp32> " LOG_RESET_COLOR;
    ESP_LOGI(TAG_CONSOLE, "Console initialized. Type 'help' to list commands.");

    while (true) {
        char* line = linenoise(prompt);
        if (line == NULL) {
            continue;
        }
        linenoiseHistoryAdd(line);

        int return_code = 0;
        esp_err_t err = esp_console_run(line, &return_code);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Unrecognized command: \"%s\"\n", line);
        } else if (err == ESP_ERR_INVALID_ARG) {
            // Empty command
        } else if (err == ESP_OK && return_code != ESP_OK) {
            printf("Command returned non-zero error code: 0x%x\n", return_code);
        } else if (err != ESP_OK) {
            printf("Internal error: 0x%x\n", err);
        }
        linenoiseFree(line);
    }
}

void initialize_console(void)
{
    static TaskHandle_t s_console_task = NULL;
    if (s_console_task != NULL) {
        ESP_LOGI(TAG_CONSOLE, "Console already running");
        return;
    }

    const uint32_t stack_size_words = 8192; // generous stack for linenoise and command handlers
    BaseType_t ok = xTaskCreatePinnedToCore(
        console_task,
        "console",
        stack_size_words,
        NULL,
        tskIDLE_PRIORITY + 1,
        &s_console_task,
        tskNO_AFFINITY
    );
    if (ok != pdPASS) {
        ESP_LOGE(TAG_CONSOLE, "Failed to create console task");
    }
}


