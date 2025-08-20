#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize device GPIOs (e.g., motion sensor interrupt)
esp_err_t init_gpio(void);

#ifdef __cplusplus
}
#endif


