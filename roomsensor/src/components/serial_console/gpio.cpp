#include <stdio.h>
#include <string.h>
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "driver/gpio.h"
#include "gpio.h"

// Common validation for pull flags
static inline bool pulls_conflict(bool up, bool down)
{
    return up && down;
}

typedef struct {
    struct arg_int *pin;
    struct arg_str *state; // high|low|hiz
    struct arg_lit *pullup;
    struct arg_lit *pulldown;
    struct arg_lit *opendrain;
    struct arg_end *end;
} gpio_set_args_t;

static gpio_set_args_t s_set_args;

static esp_err_t configure_pin_mode(gpio_num_t pin, gpio_mode_t mode, bool pullup, bool pulldown)
{
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = (1ULL << pin);
    cfg.mode = mode;
    cfg.pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = pulldown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    return gpio_config(&cfg);
}

static int gpio_set_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void**) &s_set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_set_args.end, argv[0]);
        return 1;
    }

    int pin_num = s_set_args.pin->ival[0];
    if (pin_num < 0 || pin_num > GPIO_NUM_MAX - 1) {
        printf("Invalid pin %d\n", pin_num);
        return 1;
    }
    gpio_num_t pin = (gpio_num_t) pin_num;

    const char* state = s_set_args.state->sval[0];
    bool pullup = s_set_args.pullup->count > 0;
    bool pulldown = s_set_args.pulldown->count > 0;
    bool opendrain = s_set_args.opendrain->count > 0;

    if (pulls_conflict(pullup, pulldown)) {
        printf("Cannot enable both pull-up and pull-down\n");
        return 1;
    }

    if (strcmp(state, "hiz") == 0) {
        if (configure_pin_mode(pin, GPIO_MODE_INPUT, pullup, pulldown) != ESP_OK) {
            printf("Failed to configure GPIO%d as input\n", pin_num);
            return 1;
        }
        printf("GPIO%d configured: mode=INPUT pulls=%s%s\n", pin_num,
               pullup ? "UP" : (pulldown ? "DOWN" : "NONE"), "");
        return 0;
    }

    gpio_mode_t mode = opendrain ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_OUTPUT;
    if (configure_pin_mode(pin, mode, pullup, pulldown) != ESP_OK) {
        printf("Failed to configure GPIO%d as output%s\n", pin_num, opendrain ? "_od" : "");
        return 1;
    }

    int level = 0;
    if (strcmp(state, "high") == 0) {
        level = 1;
    } else if (strcmp(state, "low") == 0) {
        level = 0;
    } else {
        printf("Invalid state '%s', expected high|low|hiz\n", state);
        return 1;
    }

    if (gpio_set_level(pin, level) != ESP_OK) {
        printf("Failed to set GPIO%d level to %d\n", pin_num, level);
        return 1;
    }
    printf("GPIO%d configured: mode=%s level=%d pulls=%s od=%s\n", pin_num,
           opendrain ? "OUTPUT_OD" : "OUTPUT",
           level,
           pullup ? "UP" : (pulldown ? "DOWN" : "NONE"),
           opendrain ? "on" : "off");
    return 0;
}

typedef struct {
    struct arg_int *pin;
    struct arg_lit *pullup;
    struct arg_lit *pulldown;
    struct arg_end *end;
} gpio_read_args_t;

static gpio_read_args_t s_read_args;

static int gpio_read_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void**) &s_read_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_read_args.end, argv[0]);
        return 1;
    }

    int pin_num = s_read_args.pin->ival[0];
    if (pin_num < 0 || pin_num > GPIO_NUM_MAX - 1) {
        printf("Invalid pin %d\n", pin_num);
        return 1;
    }
    bool pullup = s_read_args.pullup->count > 0;
    bool pulldown = s_read_args.pulldown->count > 0;
    if (pulls_conflict(pullup, pulldown)) {
        printf("Cannot enable both pull-up and pull-down\n");
        return 1;
    }

    gpio_num_t pin = (gpio_num_t) pin_num;
    if (configure_pin_mode(pin, GPIO_MODE_INPUT, pullup, pulldown) != ESP_OK) {
        printf("Failed to configure GPIO%d as input\n", pin_num);
        return 1;
    }
    int level = gpio_get_level(pin);
    printf("GPIO%d level=%d (mode=INPUT pulls=%s)\n", pin_num, level,
           pullup ? "UP" : (pulldown ? "DOWN" : "NONE"));
    return 0;
}

typedef struct {
    struct arg_int *pin;
    struct arg_end *end;
} gpio_status_args_t;

static gpio_status_args_t s_status_args;

static int gpio_status_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void**) &s_status_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, s_status_args.end, argv[0]);
        return 1;
    }

    int pin_num = s_status_args.pin->ival[0];
    if (pin_num < 0 || pin_num > GPIO_NUM_MAX - 1) {
        printf("Invalid pin %d\n", pin_num);
        return 1;
    }
    gpio_num_t pin = (gpio_num_t) pin_num;

    // Level
    int level = gpio_get_level(pin);

    // Drive capability
    gpio_drive_cap_t drive = GPIO_DRIVE_CAP_DEFAULT;
    (void) gpio_get_drive_capability(pin, &drive);

    printf("GPIO%d: mode=UNKNOWN level=%d pull=UNKNOWN drive=%d hold=UNKNOWN intr=UNKNOWN\n",
           pin_num, level, (int)drive);
    return 0;
}

void register_gpio(void)
{
    s_set_args.pin = arg_int1(NULL, NULL, "<pin>", "GPIO number");
    s_set_args.state = arg_str1(NULL, NULL, "<high|low|hiz>", "Target state");
    s_set_args.pullup = arg_lit0(NULL, "pullup", "Enable internal pull-up");
    s_set_args.pulldown = arg_lit0(NULL, "pulldown", "Enable internal pull-down");
    s_set_args.opendrain = arg_lit0(NULL, "opendrain", "Open-drain output mode (for high/low)");
    s_set_args.end = arg_end(5);

    const esp_console_cmd_t set_cmd = {
        .command = "gpio",
        .help = "GPIO control: gpio <pin> <high|low|hiz> [--pullup] [--pulldown] [--opendrain]",
        .hint = NULL,
        .func = &gpio_set_cmd,
        .argtable = &s_set_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&set_cmd) );

    s_read_args.pin = arg_int1(NULL, NULL, "<pin>", "GPIO number");
    s_read_args.pullup = arg_lit0(NULL, "pullup", "Enable internal pull-up");
    s_read_args.pulldown = arg_lit0(NULL, "pulldown", "Enable internal pull-down");
    s_read_args.end = arg_end(3);

    const esp_console_cmd_t read_cmd = {
        .command = "gpio_read",
        .help = "Read GPIO input: gpio_read <pin> [--pullup] [--pulldown]",
        .hint = NULL,
        .func = &gpio_read_cmd,
        .argtable = &s_read_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&read_cmd) );

    s_status_args.pin = arg_int1(NULL, NULL, "<pin>", "GPIO number");
    s_status_args.end = arg_end(1);

    const esp_console_cmd_t status_cmd = {
        .command = "gpio_status",
        .help = "Show GPIO configuration: gpio_status <pin>",
        .hint = NULL,
        .func = &gpio_status_cmd,
        .argtable = &s_status_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&status_cmd) );
}


