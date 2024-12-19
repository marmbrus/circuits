#pragma once

#include "esp_system.h"
#include "wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

void led_control_init(void);
void led_control_set_state(SystemState state);
void led_control_clear(void);
void led_control_stop(void);

#ifdef __cplusplus
}
#endif