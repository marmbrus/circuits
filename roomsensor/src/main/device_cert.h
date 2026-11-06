#pragma once

#include <stddef.h>
#include "esp_err.h"
#include "esp_http_client.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

// The board's own certificate, which it presents to the MQTT broker's TLS listener and to
// updates.acgt.net.
//
// util/provision.py --device-cert mints it with the YubiKey plugged into the provisioning computer
// (one of the house's certificate authorities, see the brayden repo) and writes it over the console
// (`device_cert`). It's kept as PEM in NVS namespace "device_cert": "cert", "key", and "ca" (the
// authorities the MQTT broker's certificate is checked against; updates.acgt.net has a public
// certificate). Its subject is O=acgt.net,CN=ESP_<mac>, which is how the broker and Brayden name
// the board.

// Reads the stored certificate, key and broker authorities. Call once at boot, before MQTT or OTA.
void device_cert_init(void);

// The common name this board's certificate carries: ESP_<mac>, the station MAC as in the
// sensor/<mac>/ topics.
void device_cert_common_name(char* out, size_t len);

// When the broker URI is mqtts:// and the board has a certificate, adds it, its key and the broker
// authorities to cfg.
void device_cert_configure_mqtt(esp_mqtt_client_config_t* cfg);

// Adds the certificate and key to an HTTP client's config, when the board has them.
void device_cert_configure_http(esp_http_client_config_t* cfg);

// Checks that the key belongs to the certificate, which names this board, and stores all three.
// MQTT and OTA pick them up at the next boot.
esp_err_t device_cert_store(const char* cert_pem, const char* key_pem, const char* ca_pem);

// Erases the stored certificate, key and authorities.
esp_err_t device_cert_erase(void);

// Prints this board's certificate name, and the stored certificate's subject, issuer and expiry.
void device_cert_print_status(void);

#ifdef __cplusplus
}
#endif
