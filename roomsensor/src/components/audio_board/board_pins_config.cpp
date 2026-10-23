#include <string.h>
#include "esp_log.h"
#include "board.h"
#include "audio_error.h"
#include "ConfigurationManager.h"
#include "AudioConfig.h"

static const char *TAG = "AUDIO_BOARD";

extern "C" esp_err_t get_i2c_pins(i2c_port_t port, i2c_config_t *i2c_config)
{
    // ADF only needs I2C for codecs, and these boards have none
    AUDIO_NULL_CHECK(TAG, i2c_config, return ESP_FAIL);
    i2c_config->sda_io_num = -1;
    i2c_config->scl_io_num = -1;
    return ESP_FAIL;
}

extern "C" esp_err_t get_i2s_pins(int port, board_i2s_pin_t *i2s_config)
{
    AUDIO_NULL_CHECK(TAG, i2s_config, return ESP_FAIL);
    const config::AudioConfig& audio = config::GetConfigurationManager().audio();
    if (port != 0 || !audio.has_output_pins()) {
        memset(i2s_config, -1, sizeof(board_i2s_pin_t));
        ESP_LOGE(TAG, "i2s port %d is not configured", port);
        return ESP_FAIL;
    }
    i2s_config->bck_io_num = audio.bclk_gpio();
    i2s_config->ws_io_num = audio.ws_gpio();
    i2s_config->data_out_num = audio.dout_gpio();
    i2s_config->data_in_num = audio.din_gpio();
    i2s_config->mck_io_num = -1;
    return ESP_OK;
}

extern "C" esp_err_t get_spi_pins(spi_bus_config_t *spi_config, spi_device_interface_config_t *spi_device_interface_config)
{
    AUDIO_NULL_CHECK(TAG, spi_config, return ESP_FAIL);
    AUDIO_NULL_CHECK(TAG, spi_device_interface_config, return ESP_FAIL);
    spi_config->mosi_io_num = -1;
    spi_config->miso_io_num = -1;
    spi_config->sclk_io_num = -1;
    spi_config->quadwp_io_num = -1;
    spi_config->quadhd_io_num = -1;
    spi_device_interface_config->spics_io_num = -1;
    return ESP_OK;
}

extern "C" int8_t get_pa_enable_gpio(void)
{
    return (int8_t)config::GetConfigurationManager().audio().enable_gpio();
}

extern "C" int8_t get_sdcard_intr_gpio(void) { return SDCARD_INTR_GPIO; }
extern "C" int8_t get_sdcard_open_file_num_max(void) { return SDCARD_OPEN_FILE_NUM_MAX; }
extern "C" int8_t get_sdcard_power_ctrl_gpio(void) { return SDCARD_PWR_CTRL; }
extern "C" int8_t get_headphone_detect_gpio(void) { return HEADPHONE_DETECT; }
extern "C" int8_t get_input_rec_id(void) { return BUTTON_REC_ID; }
extern "C" int8_t get_input_mode_id(void) { return BUTTON_MODE_ID; }
extern "C" int8_t get_input_set_id(void) { return BUTTON_SET_ID; }
extern "C" int8_t get_input_play_id(void) { return BUTTON_PLAY_ID; }
extern "C" int8_t get_input_volup_id(void) { return BUTTON_VOLUP_ID; }
extern "C" int8_t get_input_voldown_id(void) { return BUTTON_VOLDOWN_ID; }
extern "C" int8_t get_green_led_gpio(void) { return GREEN_LED_GPIO; }
extern "C" int8_t get_blue_led_gpio(void) { return BLUE_LED_GPIO; }
