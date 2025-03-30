#include "tas5825m.h"
#include <esp_log.h>
#include <math.h>
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_spiffs.h"
#include <stdio.h>
#include "esp_vfs.h"

static const char *TAG = "tas5825m";
static i2s_chan_handle_t tx_handle;
static i2c_master_dev_handle_t tas5825m_dev_handle = NULL;

#define TONE_TASK_STACK_SIZE  4096
#define TONE_TASK_PRIORITY    5

// Forward declare read function since write needs it
static esp_err_t tas5825m_read_reg(uint8_t reg, uint8_t *value);

static esp_err_t tas5825m_write_reg(uint8_t reg, uint8_t value) {
    ESP_LOGI(TAG, "Writing register 0x%02x with value 0x%02x", reg, value);
    uint8_t write_buf[2] = {reg, value};
    return i2c_master_transmit(tas5825m_dev_handle, write_buf, sizeof(write_buf), -1);
}

static esp_err_t tas5825m_read_reg(uint8_t reg, uint8_t *value) {
    esp_err_t ret = i2c_master_transmit_receive(tas5825m_dev_handle,
                                              &reg, 1,
                                              value, 1,
                                              -1);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Read register 0x%02x = 0x%02x", reg, *value);
    }
    return ret;
}

// Generate a sine wave sample for both channels
static void generate_sine_wave(int16_t* samples, size_t sample_count, float frequency) {
    static float phase = 0.0f;
    const float amplitude = 32767.0f; // 100% amplitude instead of 80%
    const float angular_frequency = 2.0f * M_PI * frequency / TAS5825M_SAMPLE_RATE;

    for (size_t i = 0; i < sample_count; i += 2) {
        float sample = amplitude * sinf(phase);
        // Left channel
        samples[i] = (int16_t)sample;
        // Right channel - same phase for maximum volume
        samples[i + 1] = (int16_t)sample;
        phase += angular_frequency;
        if (phase >= 2.0f * M_PI) {
            phase -= 2.0f * M_PI;
        }
    }
}

// Task to play the test tone
static void test_tone_task(void *arg) {
    ESP_LOGI(TAG, "Test tone task started");

    const size_t BUFFER_SIZE = 1024;
    int16_t *samples = heap_caps_malloc(BUFFER_SIZE * sizeof(int16_t), MALLOC_CAP_8BIT);
    if (!samples) {
        ESP_LOGE(TAG, "Failed to allocate sine wave buffer");
        vTaskDelete(NULL);
        return;
    }

    // Generate a 1 kHz sine wave at 25% amplitude
    float freq = 1000.0f;
    float phase = 0.0f;
    float step = (2.0f * M_PI * freq) / TAS5825M_SAMPLE_RATE;

    // Reduce amplitude to 5% of full scale (from 25%)
    const float amplitude = 32767.0f * 0.05f;  // 5% of full scale

    // Fill the buffer with a single cycle
    for (int i = 0; i < BUFFER_SIZE; i += 2) {
        float s = sinf(phase) * amplitude;
        samples[i]   = (int16_t)s; // Left
        samples[i+1] = (int16_t)s; // Right
        phase += step;
        if (phase >= 2.0f * M_PI) {
            phase -= 2.0f * M_PI;
        }
    }

    ESP_LOGI(TAG, "Sine wave generated. First few samples: [%d, %d, %d, %d]",
             samples[0], samples[1], samples[2], samples[3]);

    size_t bytes_written = 0;
    while (1) {
        esp_err_t err = i2s_channel_write(tx_handle, samples, BUFFER_SIZE * sizeof(int16_t),
                                        &bytes_written, portMAX_DELAY);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "i2s_channel_write error: %s", esp_err_to_name(err));
        }

        // Remove the vTaskDelay - we'll let I2S timing control the flow
        // Only log occasionally to avoid spam
        static uint32_t count = 0;
        if (++count % 1000 == 0) {
            ESP_LOGI(TAG, "Test tone playing: wrote %d bytes", bytes_written);
        }
    }

    // We should never reach here
    free(samples);
    vTaskDelete(NULL);
}

static void wav_playback_task(void *arg) {
    ESP_LOGI(TAG, "WAV playback task started");

    FILE* f = fopen("/spiffs/test.wav", "rb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open test.wav");
        vTaskDelete(NULL);
        return;
    }

    // Skip WAV header (44 bytes)
    fseek(f, 44, SEEK_SET);

    const size_t BUFFER_SIZE = 1024;
    uint8_t *buffer = heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_8BIT);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        fclose(f);
        vTaskDelete(NULL);
        return;
    }

    size_t bytes_read;
    size_t bytes_written;
    uint32_t total_bytes = 0;

    // Read and play the file
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, f)) > 0) {
        esp_err_t err = i2s_channel_write(tx_handle, buffer, bytes_read,
                                        &bytes_written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error writing to I2S: %s", esp_err_to_name(err));
            break;
        }
        total_bytes += bytes_written;
    }

    ESP_LOGI(TAG, "Finished playing WAV file, total bytes: %lu", total_bytes);

    // Send a small buffer of silence to prevent popping
    memset(buffer, 0, BUFFER_SIZE);
    i2s_channel_write(tx_handle, buffer, BUFFER_SIZE, &bytes_written, portMAX_DELAY);

    // Clean up
    free(buffer);
    fclose(f);

    // Put amplifier in HiZ state
    tas5825m_write_reg(TAS5825M_REG_DEVICE_CTRL2, 0x02);  // Set to HiZ state
    ESP_LOGI(TAG, "Amplifier set to HiZ state");

    vTaskDelete(NULL);
}

static esp_err_t tas5825m_validate_state(void) {
    uint8_t value;

    // Check if we're on the right page/book
    ESP_ERROR_CHECK(tas5825m_read_reg(TAS5825M_REG_PAGE, &value));
    ESP_LOGI(TAG, "Current page: 0x%02x", value);
    ESP_ERROR_CHECK(tas5825m_read_reg(TAS5825M_REG_BOOK, &value));
    ESP_LOGI(TAG, "Current book: 0x%02x", value);

    // Check power state
    ESP_ERROR_CHECK(tas5825m_read_reg(TAS5825M_REG_POWER, &value));
    ESP_LOGI(TAG, "Power state: 0x%02x", value);
    if (value != 0x00) {
        ESP_LOGE(TAG, "Device not powered up properly!");
        return ESP_FAIL;
    }

    // Check mute state
    ESP_ERROR_CHECK(tas5825m_read_reg(TAS5825M_REG_MUTE, &value));
    ESP_LOGI(TAG, "Mute state: 0x%02x", value);
    if (value != 0x00) {
        ESP_LOGE(TAG, "Device is still muted!");
        return ESP_FAIL;
    }

    // Check volume
    ESP_ERROR_CHECK(tas5825m_read_reg(TAS5825M_REG_VOL, &value));
    ESP_LOGI(TAG, "Volume setting: 0x%02x", value);

    return ESP_OK;
}

static esp_err_t tas5825m_validate_i2s(void) {
    uint8_t value;

    // Read I2S clock detection status (assuming register 0x07 based on similar TI amps)
    ESP_ERROR_CHECK(tas5825m_read_reg(0x07, &value));
    ESP_LOGI(TAG, "Clock status: 0x%02x", value);

    // Read error status registers if available
    ESP_ERROR_CHECK(tas5825m_read_reg(0x08, &value));
    ESP_LOGI(TAG, "Error status: 0x%02x", value);

    return ESP_OK;
}

// Add these register definitions
#define TAS5825M_REG_DSP_CTRL    0x00   // Need to verify actual register address
#define TAS5825M_REG_PLAY_STATE  0x00   // Need to verify actual register address

static esp_err_t tas5825m_set_book_page(uint8_t book, uint8_t page) {
    uint8_t value;

    // Set and verify book
    tas5825m_write_reg(TAS5825M_REG_BOOK, book);
    vTaskDelay(pdMS_TO_TICKS(1));
    tas5825m_read_reg(TAS5825M_REG_BOOK, &value);
    if (value != book) {
        ESP_LOGW(TAG, "Book set failed: wrote 0x%02x, read 0x%02x", book, value);
        return ESP_FAIL;
    }

    // Set and verify page
    tas5825m_write_reg(TAS5825M_REG_PAGE, page);
    vTaskDelay(pdMS_TO_TICKS(1));
    tas5825m_read_reg(TAS5825M_REG_PAGE, &value);
    if (value != page) {
        ESP_LOGW(TAG, "Page set failed: wrote 0x%02x, read 0x%02x", page, value);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t tas5825m_check_clocks(void) {
    uint8_t clkdet_status;
    int retry = 0;
    const int max_retries = 10;  // Try for up to 100ms

    // Wait for PLL to lock
    while (retry++ < max_retries) {
        ESP_ERROR_CHECK(tas5825m_read_reg(0x39, &clkdet_status));
        ESP_LOGI(TAG, "Clock Detection Status: 0x%02x (retry %d)", clkdet_status, retry);

        if (!(clkdet_status & 0x08)) {  // PLL is locked
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));  // Wait 10ms between checks
    }

    if (retry >= max_retries) {
        ESP_LOGE(TAG, "Timeout waiting for PLL lock");
        return ESP_FAIL;
    }

    // Now check the rest of the clock configuration
    uint8_t fs_mon, bck_mon;

    // Read FS_MON register (0x37)
    ESP_ERROR_CHECK(tas5825m_read_reg(0x37, &fs_mon));
    ESP_LOGI(TAG, "FS Monitor: 0x%02x", fs_mon);

    // Decode FS value
    uint8_t fs = fs_mon & 0x0F;
    const char* fs_str;
    switch(fs) {
        case 0x09: fs_str = "48kHz"; break;
        case 0x0B: fs_str = "96kHz"; break;
        case 0x0D: fs_str = "192kHz"; break;
        case 0x00: fs_str = "FS Error"; break;
        default: fs_str = "Unknown"; break;
    }
    ESP_LOGI(TAG, "Detected sample rate: %s", fs_str);

    // Read BCK_MON register (0x38)
    ESP_ERROR_CHECK(tas5825m_read_reg(0x38, &bck_mon));
    ESP_LOGI(TAG, "BCK Monitor: 0x%02x", bck_mon);

    // BCK ratio = number of BCLK cycles per LRCK period
    uint16_t bck_ratio = ((fs_mon & 0x30) << 4) | bck_mon;
    ESP_LOGI(TAG, "BCLK ratio: %d", bck_ratio);

    // Check for valid sample rate
    if (fs == 0x00) {
        ESP_LOGE(TAG, "Sample rate error detected");
        return ESP_FAIL;
    }

    // Check BCLK ratio is in valid range (32-512)
    if (bck_ratio < 32 || bck_ratio > 512) {
        ESP_LOGE(TAG, "BCLK ratio %d out of valid range (32-512)", bck_ratio);
        return ESP_FAIL;
    }

    // Check for any clock detection issues
    if (clkdet_status != 0) {
        ESP_LOGE(TAG, "Clock detection issues present: 0x%02x", clkdet_status);
        if (clkdet_status & 0x01) ESP_LOGE(TAG, "  FS Error");
        if (clkdet_status & 0x02) ESP_LOGE(TAG, "  SCLK Invalid");
        if (clkdet_status & 0x04) ESP_LOGE(TAG, "  SCLK Missing");
        if (clkdet_status & 0x08) ESP_LOGE(TAG, "  PLL Unlocked");
        if (clkdet_status & 0x10) ESP_LOGE(TAG, "  PLL Overrange");
        if (clkdet_status & 0x20) ESP_LOGE(TAG, "  SCLK Overrange");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Clock configuration verified successfully (BCLK ratio: %d, FS: %s)",
             bck_ratio, fs_str);
    return ESP_OK;
}

static esp_err_t tas5825m_read_and_log(uint8_t reg)
{
    uint8_t value;
    esp_err_t ret = tas5825m_read_reg(reg, &value);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Read register 0x%02X = 0x%02X", reg, value);
    } else {
        ESP_LOGE(TAG, "Failed to read register 0x%02X", reg);
    }
    return ret;
}

esp_err_t tas5825m_init(i2c_master_bus_handle_t bus_handle)
{
    ESP_LOGI(TAG, "=== TAS5825M Initialization Start ===");

    // Create I2C device handle if not done elsewhere
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TAS5825M_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &tas5825m_dev_handle));

    // 1. Hard reset - reset registers, then reset the digital core
    ESP_LOGI(TAG, "Performing full register reset, then digital core reset");
    esp_err_t ret = tas5825m_write_reg(TAS5825M_REG_RESET_CTRL, 0x01); // Reset registers
    ESP_LOGI(TAG, "Wrote 0x01 to RESET_CTRL (0x01)");
    vTaskDelay(pdMS_TO_TICKS(10));

    ret = tas5825m_write_reg(TAS5825M_REG_RESET_CTRL, 0x10); // Reset digital core
    ESP_LOGI(TAG, "Wrote 0x10 to RESET_CTRL (digital core reset)");
    vTaskDelay(pdMS_TO_TICKS(10));

    // 2. Configure output mode (BTL, 384kHz, BD Mode if needed)
    ESP_LOGI(TAG, "Configuring output mode in DEVICE_CTRL1 (0x02)");
    ret = tas5825m_write_reg(TAS5825M_REG_DEVICE_CTRL1, 0x00);
    if (ret != ESP_OK) return ret;
    tas5825m_read_and_log(TAS5825M_REG_DEVICE_CTRL1);

    // 3. Set initial power state to Deep Sleep
    ESP_LOGI(TAG, "Setting power state to Deep Sleep in DEVICE_CTRL2 (0x03)");
    ret = tas5825m_write_reg(TAS5825M_REG_DEVICE_CTRL2, 0x00);
    if (ret != ESP_OK) return ret;
    tas5825m_read_and_log(TAS5825M_REG_DEVICE_CTRL2);
    vTaskDelay(pdMS_TO_TICKS(5));

    // 4. Configure I2S interface registers
    ESP_LOGI(TAG, "Configuring SAP_CTRL1 (0x33) for 16-bit I2S");
    ret = tas5825m_write_reg(TAS5825M_REG_SAP_CTRL1, 0x00); // 16-bit I2S (word_length=00, data_format=00)
    if (ret != ESP_OK) return ret;
    tas5825m_read_and_log(TAS5825M_REG_SAP_CTRL1);

    // 5. Start I2S driver
    ESP_LOGI(TAG, "Starting I2S driver (master mode)");
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(TAS5825M_SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(16, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = TAS5825M_SCLK_GPIO,
            .ws   = TAS5825M_LRCLK_GPIO,
            .dout = TAS5825M_SDIN_GPIO,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    vTaskDelay(pdMS_TO_TICKS(20));

    // 6. Transition to HiZ
    ESP_LOGI(TAG, "Transitioning to HiZ state in DEVICE_CTRL2 (0x03)");
    ret = tas5825m_write_reg(TAS5825M_REG_DEVICE_CTRL2, 0x02);
    if (ret != ESP_OK) return ret;
    tas5825m_read_and_log(TAS5825M_REG_DEVICE_CTRL2);
    vTaskDelay(pdMS_TO_TICKS(5));

    // 7. Enable DSP in ROM mode 1
    ESP_LOGI(TAG, "Enabling DSP in DSP_PGM_MODE (0x40)");
    ret = tas5825m_write_reg(TAS5825M_REG_DSP_PGM_MODE, 0x01);
    if (ret != ESP_OK) return ret;
    tas5825m_read_and_log(TAS5825M_REG_DSP_PGM_MODE);
    vTaskDelay(pdMS_TO_TICKS(5));

    // 8. Configure GPIO pins for FAULTZ/WARNZ
    ESP_LOGI(TAG, "Configuring GPIO pins");
    ret = tas5825m_write_reg(TAS5825M_REG_GPIO_CTRL, 0x06); // GPIO1 & GPIO2 as outputs
    if (ret != ESP_OK) return ret;
    tas5825m_read_and_log(TAS5825M_REG_GPIO_CTRL);

    ret = tas5825m_write_reg(TAS5825M_REG_GPIO1_SEL, 0x0B); // GPIO1 = FAULTZ
    if (ret != ESP_OK) return ret;
    tas5825m_read_and_log(TAS5825M_REG_GPIO1_SEL);

    ret = tas5825m_write_reg(TAS5825M_REG_GPIO2_SEL, 0x08); // GPIO2 = WARNZ
    if (ret != ESP_OK) return ret;
    tas5825m_read_and_log(TAS5825M_REG_GPIO2_SEL);

    // 9. Set digital volume & disable auto-mute
    ESP_LOGI(TAG, "Setting digital volume (DIG_VOL) and disabling auto mute (0x50)");
    ret = tas5825m_write_reg(TAS5825M_REG_DIG_VOL, 0x50); // Reduce from 0x30 to 0x50 (-32dB)
    if (ret != ESP_OK) return ret;
    tas5825m_read_and_log(TAS5825M_REG_DIG_VOL);

    ret = tas5825m_write_reg(0x50, 0x00); // AUTO_MUTE_CTRL
    if (ret != ESP_OK) return ret;
    tas5825m_read_and_log(0x50);

    // 10. Configure audio routing (SAP_CTRL3 = 0x35)
    ESP_LOGI(TAG, "Mapping left channel to left DAC, right to right DAC (SAP_CTRL3)");
    ret = tas5825m_write_reg(0x35, 0x11); // LEFT_DAC=01, RIGHT_DAC=01
    if (ret != ESP_OK) return ret;
    tas5825m_read_and_log(0x35);

    // 11. Clear any existing faults
    ESP_LOGI(TAG, "Clearing existing faults (FAULT_CLEAR=0x78)");
    ret = tas5825m_write_reg(0x78, 0x80);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(5));

    // 12. Transition to PLAY
    ESP_LOGI(TAG, "Transitioning to PLAY state in DEVICE_CTRL2 (0x03)");
    ret = tas5825m_write_reg(TAS5825M_REG_DEVICE_CTRL2, 0x03);
    if (ret != ESP_OK) return ret;
    tas5825m_read_and_log(TAS5825M_REG_DEVICE_CTRL2);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Read final power state (POWER_STATE=0x68)
    uint8_t power_state;
    tas5825m_read_reg(0x68, &power_state);
    ESP_LOGI(TAG, "Final POWER_STATE (0x68) = 0x%02X [3=PLAY]", power_state);

    // Read final status registers
    uint8_t clkdet_status, fault1, fault2, warning;
    tas5825m_read_reg(TAS5825M_REG_CLKDET_STATUS, &clkdet_status);
    ESP_LOGI(TAG, "CLKDET_STATUS (0x39) = 0x%02x", clkdet_status);
    tas5825m_read_reg(TAS5825M_REG_GLOBAL_FAULT1, &fault1);
    ESP_LOGI(TAG, "GLOBAL_FAULT1 (0x71) = 0x%02x", fault1);
    tas5825m_read_reg(TAS5825M_REG_GLOBAL_FAULT2, &fault2);
    ESP_LOGI(TAG, "GLOBAL_FAULT2 (0x72) = 0x%02x", fault2);
    tas5825m_read_reg(TAS5825M_REG_WARNING, &warning);
    ESP_LOGI(TAG, "WARNING (0x73) = 0x%02x", warning);

    ESP_LOGI(TAG, "=== TAS5825M Initialization Complete ===");
    return ESP_OK;
}

esp_err_t tas5825m_play_test_tone(void) {
    ESP_LOGI(TAG, "Creating test tone task");

    BaseType_t ret = xTaskCreate(test_tone_task,
                                "test_tone",
                                4096,           // Stack size
                                NULL,           // Parameters
                                5,              // Priority
                                NULL);          // Task handle

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create test tone task! error=%d", ret);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Test tone task created successfully");
    return ESP_OK;
}

esp_err_t tas5825m_play_wav(void) {
    ESP_LOGI(TAG, "Creating WAV playback task");

    BaseType_t ret = xTaskCreate(wav_playback_task,
                                "wav_player",
                                4096,
                                NULL,
                                5,
                                NULL);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WAV playback task!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "WAV playback task created successfully");
    return ESP_OK;
}