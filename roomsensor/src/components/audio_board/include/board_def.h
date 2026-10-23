/*
 * Compile-time board definitions required by ESP-ADF components.
 *
 * Nothing board-specific lives here: I2S and amp-enable pins come from the "audio"
 * configuration module at runtime (see board_pins_config.cpp). The boards have no
 * audio codec, SD card, buttons, or ADF-managed LEDs, so those are all disabled.
 */
#ifndef _AUDIO_BOARD_DEFINITION_H_
#define _AUDIO_BOARD_DEFINITION_H_

#define FUNC_AUDIO_CODEC_EN         (0)
#define HEADPHONE_DETECT            (-1)
#define BOARD_PA_GAIN               (0) /* Only used by ADF codec drivers (dB) */

#define FUNC_SDCARD_EN              (0)
#define SDCARD_OPEN_FILE_NUM_MAX    5
#define SDCARD_INTR_GPIO            -1
#define SDCARD_PWR_CTRL             -1
#define ESP_SD_PIN_CLK              -1
#define ESP_SD_PIN_CMD              -1
#define ESP_SD_PIN_D0               -1
#define ESP_SD_PIN_D1               -1
#define ESP_SD_PIN_D2               -1
#define ESP_SD_PIN_D3               -1
#define ESP_SD_PIN_D4               -1
#define ESP_SD_PIN_D5               -1
#define ESP_SD_PIN_D6               -1
#define ESP_SD_PIN_D7               -1
#define ESP_SD_PIN_CD               -1
#define ESP_SD_PIN_WP               -1

#define FUNC_BUTTON_EN              (0)
#define BUTTON_VOLUP_ID             -1
#define BUTTON_VOLDOWN_ID           -1
#define BUTTON_SET_ID               -1
#define BUTTON_PLAY_ID              -1
#define BUTTON_MODE_ID              -1
#define BUTTON_REC_ID               -1

#define GREEN_LED_GPIO              -1
#define BLUE_LED_GPIO               -1

#endif
