#include "esp_log.h"
#include "board.h"
#include "audio_mem.h"

static const char *TAG = "AUDIO_BOARD";

static audio_board_handle_t board_handle = NULL;

audio_board_handle_t audio_board_init(void)
{
    if (board_handle) {
        ESP_LOGW(TAG, "The board has already been initialized!");
        return board_handle;
    }
    board_handle = (audio_board_handle_t) audio_calloc(1, sizeof(struct audio_board_handle));
    AUDIO_MEM_CHECK(TAG, board_handle, return NULL);
    board_handle->audio_hal = NULL;
    return board_handle;
}

audio_hal_handle_t audio_board_codec_init(void)
{
    return NULL;
}

display_service_handle_t audio_board_led_init(void)
{
    return NULL;
}

esp_err_t audio_board_key_init(esp_periph_set_handle_t set)
{
    return ESP_ERR_NOT_SUPPORTED;
}

audio_board_handle_t audio_board_get_handle(void)
{
    return board_handle;
}

esp_err_t audio_board_deinit(audio_board_handle_t audio_board)
{
    audio_free(audio_board);
    board_handle = NULL;
    return ESP_OK;
}
