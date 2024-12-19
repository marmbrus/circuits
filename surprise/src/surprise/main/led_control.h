#pragma once

#include "esp_system.h"
#include "wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

void led_control_init(void);
void led_control_set_state(SystemState state);

#ifdef __cplusplus
}
#endif