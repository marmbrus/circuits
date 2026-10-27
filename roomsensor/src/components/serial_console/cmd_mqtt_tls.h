#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Register the mqtt_tls console command, which provisions the MQTT client certificate
void register_mqtt_tls(void);

#ifdef __cplusplus
}
#endif
