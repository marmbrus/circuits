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
#include "cmd_ota.h"
#include "gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "console_buffer.h"
#include "esp_timer.h"
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

static const char* TAG_CONSOLE = "console";

// Enum for console mode
typedef enum {
    CONSOLE_MODE_SIMPLE,
    CONSOLE_MODE_INTERACTIVE,
} console_mode_t;

// Global variable to hold the current console mode, defaulting to simple
static console_mode_t s_console_mode = CONSOLE_MODE_SIMPLE;

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

// Helper to run a command and handle errors
static void run_command(char *line) {
    int return_code = 0;
    esp_err_t err = esp_console_run(line, &return_code);
    if (err == ESP_ERR_NOT_FOUND) {
        printf("Unrecognized command: \"%s\"\n", line);
        console_buffer_append_str("Unrecognized command\n", CONSOLE_DIR_OUT);
    } else if (err == ESP_ERR_INVALID_ARG) {
        // Empty command
    } else if (err == ESP_OK && return_code != ESP_OK) {
        printf("Command returned non-zero error code: 0x%x\n", return_code);
        char tmp[64];
        int m = snprintf(tmp, sizeof(tmp), "Command returned error: 0x%x\n", return_code);
        if (m > 0) console_buffer_append(tmp, (size_t)m, CONSOLE_DIR_OUT);
    } else if (err != ESP_OK) {
        printf("Internal error: 0x%x\n", err);
        char tmp[64];
        int m = snprintf(tmp, sizeof(tmp), "Internal error: 0x%x\n", err);
        if (m > 0) console_buffer_append(tmp, (size_t)m, CONSOLE_DIR_OUT);
    }
}

// Command handler for console_mode
static int console_mode_cmd(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: console_mode <interactive|simple>\\n");
        return 1;
    }
    if (strcmp(argv[1], "interactive") == 0) {
        if (s_console_mode == CONSOLE_MODE_INTERACTIVE) {
            printf("Already in interactive mode.\\n");
        } else {
            s_console_mode = CONSOLE_MODE_INTERACTIVE;
            printf("Switched to interactive mode. Press Enter to activate.\\n");
        }
    } else if (strcmp(argv[1], "simple") == 0) {
        if (s_console_mode == CONSOLE_MODE_SIMPLE) {
            printf("Already in simple mode.\\n");
        } else {
            s_console_mode = CONSOLE_MODE_SIMPLE;
            printf("Switched to simple mode. Reconnect if terminal is unresponsive.\\n");
        }
    } else {
        printf("Unknown mode: %s\\n", argv[1]);
        return 1;
    }
    return 0;
}

// Command handler for tty command to switch to interactive mode
static int tty_cmd(int argc, char **argv) {
    if (s_console_mode == CONSOLE_MODE_INTERACTIVE) {
        printf("Already in interactive mode.\\n");
    } else {
        s_console_mode = CONSOLE_MODE_INTERACTIVE;
        printf("Switched to interactive mode. Press Enter to activate.\\n");
    }
    return 0;
}

static void register_console_commands(void) {
    const esp_console_cmd_t mode_cmd = {
        .command = "console_mode",
        .help = "Set console mode (simple/interactive)",
        .hint = NULL,
        .func = &console_mode_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&mode_cmd));

    const esp_console_cmd_t tty_cmd_s = {
        .command = "tty",
        .help = "Switch to interactive TTY mode with linenoise",
        .hint = NULL,
        .func = &tty_cmd,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&tty_cmd_s));
}


static void console_task(void *arg)
{
    configure_stdio_uart();

    // Initialize circular buffer in SPIRAM (128 KB)
    (void)console_buffer_init(128 * 1024);

    esp_console_config_t console_config = {
        .max_cmdline_length = 256,
        .max_cmdline_args = 16,
        .hint_color = atoi(LOG_COLOR_CYAN),
        .hint_bold = 0,
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));

    esp_console_register_help_command();

    // Register standard command sets
    register_system();
    register_nvs();
    register_wifi();
    register_gpio();
    register_ota();
    register_console_commands();

    ESP_LOGI(TAG_CONSOLE, "Console initialized. Type 'help' to list commands.");

    const char* prompt = LOG_COLOR_I "esp32> " LOG_RESET_COLOR;

    // Loop indefinitely, checking the mode at the start of each line input
    while (true) {
        if (s_console_mode == CONSOLE_MODE_INTERACTIVE) {
            linenoiseSetMultiLine(1);
            linenoiseHistorySetMaxLen(100);
            linenoiseSetCompletionCallback(&esp_console_get_completion);
            linenoiseSetHintsCallback((linenoiseHintsCallback*)&esp_console_get_hint);
            
            char* line = linenoise(prompt);
            if (line == NULL) { // NULL on heap exhaustion, ENOMEM, or Ctrl-D
                // On Ctrl-D, switch back to simple mode as linenoise exits.
                s_console_mode = CONSOLE_MODE_SIMPLE;
                printf("\\nExited interactive mode.\\n");
                continue;
            }
            if (strlen(line) == 0) {
                free(line);
                continue;
            }
            
            console_buffer_append(line, strlen(line), CONSOLE_DIR_IN);
            console_buffer_append("\\n", 1, CONSOLE_DIR_IN);
            linenoiseHistoryAdd(line);

            run_command(line);
            
            linenoiseFree(line);
        } else {
            // Simple mode with basic echo and backspace support
            char line[256];
            int i = 0;
            printf("%s", prompt);
            fflush(stdout);
            
            while (i < sizeof(line) - 1) {
                int c = fgetc(stdin);
                if (c < 0) { // End of file or error
                    // Don't busy-wait if no char is available
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                    continue;
                }

                if (c == '\r' || c == '\n') {
                    printf("\r\n");
                    fflush(stdout);
                    break;
                } else if (c == '\b' || c == 127) { // Backspace
                    if (i > 0) {
                        i--;
                        // Erase character on terminal
                        printf("\b \b");
                        fflush(stdout);
                    }
                } else if (isprint(c)) {
                    line[i++] = (char)c;
                    putchar((char)c);
                    fflush(stdout);
                }
            }
            line[i] = '\0';

            if (strlen(line) == 0) {
                continue;
            }
            
            console_buffer_append(line, strlen(line), CONSOLE_DIR_IN);
            console_buffer_append("\\n", 1, CONSOLE_DIR_IN);
            run_command(line);
        }
    }

    ESP_LOGE(TAG_CONSOLE, "Error or end-of-input, stopping console");
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


