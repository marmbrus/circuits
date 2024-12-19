#pragma once

#include "esp_system.h"
#include "wifi.h"
#include "LEDBehavior.h"

#ifdef __cplusplus
extern "C" {
#endif

void led_control_init(void);
void led_control_set_state(SystemState state);
void led_control_clear(void);
void led_control_stop(void);
void led_control_set_button_led_status(int index, bool status);
void led_control_set_behavior(LEDBehavior* behavior);

#ifdef __cplusplus
}
#endif