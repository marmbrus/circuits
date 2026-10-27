#pragma once

#include <stddef.h>
#include "esp_err.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

// The client certificate for the MQTT broker's TLS listener.
//
// util/provision.py --mqtt-cert mints it with the YubiKey plugged into the provisioning computer
// (one of the house's certificate authorities, see the brayden repo) and writes it over the console
// (`mqtt_tls`). It's kept as PEM in NVS namespace "mqtt_tls": "cert", "key", and "ca" (the
// authorities the broker's certificate is checked against). The broker names the client by the
// certificate's subject, O=acgt.net,CN=ESP_<mac>.

// The common name this device's certificate carries: ESP_<mac>, the station MAC as in the
// sensor/<mac>/ topics.
void mqtt_tls_common_name(char* out, size_t len);

// When the broker URI is mqtts://, adds the stored certificate, key and authorities to cfg. They
// stay allocated for the rest of the program, since the MQTT client keeps pointers to them.
void mqtt_tls_configure(esp_mqtt_client_config_t* cfg);

// Checks that the key belongs to the certificate, which names this device, and stores all three.
// The MQTT client picks them up at the next boot.
esp_err_t mqtt_tls_store(const char* cert_pem, const char* key_pem, const char* ca_pem);

// Erases the stored certificate, key and authorities.
esp_err_t mqtt_tls_erase(void);

// Prints this device's certificate name, and the stored certificate's subject, issuer and expiry.
void mqtt_tls_print_status(void);

#ifdef __cplusplus
}
#endif
