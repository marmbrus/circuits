/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Console example — WiFi commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "cmd_wifi.h"

/**
 * This component will be supported using esp_wifi_remote
 */
#if CONFIG_SOC_WIFI_SUPPORTED

#define JOIN_TIMEOUT_MS (10000)

static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;


static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    }
}

static void initialise_wifi(void)
{
    esp_log_level_set("wifi", ESP_LOG_WARN);
    static bool initialized = false;
    if (initialized) {
        return;
    }

    // If Wi-Fi is already initialized by the application, skip re-init
    wifi_mode_t dummy_mode;
    bool wifi_already_inited = (esp_wifi_get_mode(&dummy_mode) != ESP_ERR_WIFI_NOT_INIT);

    if (!wifi_already_inited) {
        (void) esp_netif_init(); // ignore ESP_ERR_INVALID_STATE
        wifi_event_group = xEventGroupCreate();
        (void) esp_event_loop_create_default(); // ignore ESP_ERR_INVALID_STATE
        esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
        assert(ap_netif);
        esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
        assert(sta_netif);
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&cfg) != ESP_OK) {
            // If already initialized by app, continue
        }
        (void) esp_wifi_set_storage(WIFI_STORAGE_RAM);
        (void) esp_wifi_set_mode(WIFI_MODE_NULL);
        (void) esp_wifi_start();
    }

    if (wifi_event_group == NULL) {
        wifi_event_group = xEventGroupCreate();
    }

    (void) esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event_handler, NULL);
    (void) esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);
    initialized = true;
}

static bool wifi_join(const char *ssid, const char *pass, int timeout_ms)
{
    initialise_wifi();
    wifi_config_t wifi_config = { 0 };
    strlcpy((char *) wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    if (pass) {
        strlcpy((char *) wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    }

    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    esp_wifi_connect();

    int bits = xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,
                                   pdFALSE, pdTRUE, timeout_ms / portTICK_PERIOD_MS);
    return (bits & CONNECTED_BIT) != 0;
}

/** Arguments used by 'join' function */
static struct {
    struct arg_int *timeout;
    struct arg_str *ssid;
    struct arg_str *password;
    struct arg_end *end;
} join_args;

/** Arguments used by 'get_mac' function */
static struct {
    struct arg_str *iface;
    struct arg_end *end;
} getmac_args;

static int get_mac_cmd(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &getmac_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, getmac_args.end, argv[0]);
        return 1;
    }

    initialise_wifi();

    wifi_interface_t iface = WIFI_IF_STA;
    if (getmac_args.iface->count) {
        const char *val = getmac_args.iface->sval[0];
        if (strcmp(val, "ap") == 0) {
            iface = WIFI_IF_AP;
        } else if (strcmp(val, "sta") != 0) {
            printf("Invalid interface '%s', expected 'sta' or 'ap'\n", val);
            return 1;
        }
    }

    uint8_t mac[6] = {0};
    esp_err_t err = esp_wifi_get_mac(iface, mac);
    if (err != ESP_OK) {
        printf("esp_wifi_get_mac failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("%02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return 0;
}

static int disconnect_cmd(int argc, char **argv)
{
    (void) argc; (void) argv;
    initialise_wifi();
    esp_err_t err = esp_wifi_disconnect();
    if (err == ESP_ERR_WIFI_NOT_CONNECT) {
        printf("Not connected\n");
        return 0;
    }
    if (err != ESP_OK) {
        printf("esp_wifi_disconnect failed: %s\n", esp_err_to_name(err));
        return 1;
    }
    printf("Disconnect requested\n");
    return 0;
}

static int connect(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &join_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, join_args.end, argv[0]);
        return 1;
    }
    ESP_LOGI(__func__, "Connecting to '%s'",
             join_args.ssid->sval[0]);

    /* set default value*/
    if (join_args.timeout->count == 0) {
        join_args.timeout->ival[0] = JOIN_TIMEOUT_MS;
    }

    bool connected = wifi_join(join_args.ssid->sval[0],
                               join_args.password->sval[0],
                               join_args.timeout->ival[0]);
    if (!connected) {
        ESP_LOGW(__func__, "Connection timed out");
        return 1;
    }
    ESP_LOGI(__func__, "Connected");
    return 0;
}

void register_wifi(void)
{
    join_args.timeout = arg_int0(NULL, "timeout", "<t>", "Connection timeout, ms");
    join_args.ssid = arg_str1(NULL, NULL, "<ssid>", "SSID of AP");
    join_args.password = arg_str0(NULL, NULL, "<pass>", "PSK of AP");
    join_args.end = arg_end(2);

    const esp_console_cmd_t join_cmd = {
        .command = "join",
        .help = "Join WiFi AP as a station",
        .hint = NULL,
        .func = &connect,
        .argtable = &join_args
    };

    ESP_ERROR_CHECK( esp_console_cmd_register(&join_cmd) );

    // get_mac command: prints MAC of STA (default) or AP interface
    getmac_args.iface = arg_str0(NULL, NULL, "[sta|ap]", "Network interface (default: sta)");
    getmac_args.end = arg_end(1);


    const esp_console_cmd_t get_mac_cmd_def = {
        .command = "get_mac",
        .help = "Print MAC address of STA (default) or AP interface. Usage: get_mac [sta|ap]",
        .hint = NULL,
        .func = &get_mac_cmd,
        .argtable = &getmac_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&get_mac_cmd_def) );

    const esp_console_cmd_t disconnect_cmd_def = {
        .command = "disconnect",
        .help = "Disconnect from current WiFi AP",
        .hint = NULL,
        .func = &disconnect_cmd,
        .argtable = NULL
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&disconnect_cmd_def) );
}

#endif // CONFIG_SOC_WIFI_SUPPORTED
