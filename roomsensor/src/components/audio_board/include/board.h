#ifndef _AUDIO_BOARD_H_
#define _AUDIO_BOARD_H_

#include "audio_hal.h"
#include "board_def.h"
#include "board_pins_config.h"
#include "esp_peripherals.h"
#include "display_service.h"

#ifdef __cplusplus
extern "C" {
#endif

struct audio_board_handle {
    audio_hal_handle_t audio_hal; /*!< Always NULL: there is no codec to configure */
};

typedef struct audio_board_handle *audio_board_handle_t;

audio_board_handle_t audio_board_init(void);
audio_board_handle_t audio_board_get_handle(void);
esp_err_t audio_board_deinit(audio_board_handle_t audio_board);

/* Not available on these boards; present because ADF components reference them. */
audio_hal_handle_t audio_board_codec_init(void);
display_service_handle_t audio_board_led_init(void);
esp_err_t audio_board_key_init(esp_periph_set_handle_t set);

#ifdef __cplusplus
}
#endif

#endif
