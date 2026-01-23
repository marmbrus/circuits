#include "cmd_tlc59711.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include <cstring>
#include <vector>

static const char* TAG = "cmd_tlc59711";

// Configuration similar to the test script
static constexpr spi_host_device_t kSpiHost = SPI3_HOST;
static constexpr gpio_num_t kPinMosi = GPIO_NUM_11; // SDTI
static constexpr gpio_num_t kPinSclk = GPIO_NUM_12; // SCKI
static constexpr gpio_num_t kPinPowerEnable = GPIO_NUM_15;

static constexpr int kPacketBits = 224;
static constexpr int kPacketBytes = kPacketBits / 8;
static constexpr uint8_t kWriteCommand = 0x25; // 6-bit write command
static constexpr uint8_t kBcMax = 0x7F; // 100% current

// Structure for parsing arguments
static struct {
    struct arg_int* channels; // 16-bit values for channels
    struct arg_lit* init;     // Initialize hardware flag
    struct arg_end* end;
} tlc_args;

// Helper struct for bit packing
struct BitWriter {
    uint8_t* data;
    int bit_pos;
};

static void set_stream_bit(uint8_t* data, int stream_bit, bool value) {
    const int byte_index = stream_bit / 8;
    const int bit_in_byte = 7 - (stream_bit % 8);
    if (value) {
        data[byte_index] |= static_cast<uint8_t>(1U << bit_in_byte);
    }
}

static void push_bits(BitWriter& writer, uint32_t value, int count) {
    for (int bit = count - 1; bit >= 0; --bit) {
        set_stream_bit(writer.data, writer.bit_pos, (value >> bit) & 0x1);
        ++writer.bit_pos;
    }
}

static void push_word(BitWriter& writer, uint16_t value) {
    push_bits(writer, value, 16);
}

static void build_packet(uint8_t* out, const uint16_t* channels) {
    std::memset(out, 0, kPacketBytes);

    BitWriter writer{out, 0};

    // Write command (6 bits, MSB first).
    push_bits(writer, kWriteCommand, 6);

    // Function control bits (5 bits, MSB first).
    // OUTTMG=1, EXTGCK=0, TMGRST=0, DSPRPT=1, BLANK=0 => 0b10010
    const uint8_t fc = 0b10010;
    push_bits(writer, fc, 5);

    // Global brightness (BC) for B/G/R (7 bits each, MSB first).
    push_bits(writer, kBcMax, 7); // Blue
    push_bits(writer, kBcMax, 7); // Green
    push_bits(writer, kBcMax, 7); // Red

    // Grayscale (GS) for all 12 channels (16 bits each, MSB first).
    // Stream order is MSB-first: Channel 11 -> Channel 0.
    for (int i = 11; i >= 0; --i) {
        push_word(writer, channels[i]);
    }
}

static void log_channels_hex(int chip_idx, const uint16_t* channels) {
    ESP_LOGI(TAG, "Chip %d channels (index 0..11), hex:", chip_idx);
    for (int i = 0; i < 12; ++i) {
        ESP_LOGI(TAG, "  ch[%02d]=0x%04X", i, channels[i]);
    }
}

static void log_packet_hex(int chip_idx, const uint8_t* packet) {
    ESP_LOGI(TAG, "Chip %d packet (224 bits) encoded MSB-first:", chip_idx);
    esp_log_buffer_hex(TAG, packet, kPacketBytes);
}

static spi_device_handle_t spi_handle = nullptr;
static bool is_initialized = false;

static esp_err_t init_hardware() {
    if (is_initialized) return ESP_OK;

    // Power enable
    gpio_reset_pin(kPinPowerEnable);
    gpio_set_direction(kPinPowerEnable, GPIO_MODE_OUTPUT);
    gpio_set_level(kPinPowerEnable, 1);
    esp_rom_delay_us(1000);
    ESP_LOGI(TAG, "Power enable set on GPIO %d", static_cast<int>(kPinPowerEnable));

    // SPI Bus Init
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = kPinMosi;
    bus_cfg.miso_io_num = -1;
    bus_cfg.sclk_io_num = kPinSclk;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4092; // Enough for many chips
    bus_cfg.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_MOSI | SPICOMMON_BUSFLAG_SCLK;

    // Initialize bus (ignore if already initialized)
    esp_err_t err = spi_bus_initialize(kSpiHost, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    // Add Device
    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.clock_speed_hz = 1 * 1000 * 1000; // 1 MHz conservative
    dev_cfg.mode = 0;
    dev_cfg.spics_io_num = -1;
    dev_cfg.queue_size = 1;
    dev_cfg.flags = SPI_DEVICE_NO_DUMMY;

    err = spi_bus_add_device(kSpiHost, &dev_cfg, &spi_handle);
    if (err == ESP_OK) {
        is_initialized = true;
        ESP_LOGI(TAG, "TLC59711 hardware initialized");
    }
    return err;
}

static int tlc59711_cmd(int argc, char** argv) {
    int nerrors = arg_parse(argc, argv, (void**)&tlc_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, tlc_args.end, argv[0]);
        return 1;
    }

    // Handle initialization request
    if (tlc_args.init->count > 0) {
        if (init_hardware() != ESP_OK) {
            ESP_LOGE(TAG, "Hardware initialization failed");
            return 1;
        }
    } else if (!is_initialized) {
        // Auto-initialize if channel data provided but not inited
        if (tlc_args.channels->count > 0) {
             if (init_hardware() != ESP_OK) {
                ESP_LOGE(TAG, "Hardware initialization failed");
                return 1;
            }
        } else {
            ESP_LOGE(TAG, "Hardware not initialized. Use --init or provide channel data.");
            return 1;
        }
    }

    if (tlc_args.channels->count == 0) {
        return 0; // Just init, nothing to send
    }

    int num_values = tlc_args.channels->count;
    int num_chips = (num_values + 11) / 12; // Ceiling division
    
    if (num_chips > 5) { // Requirement: up to 5 chips
        ESP_LOGE(TAG, "Too many channels provided. Max support is 5 chips (60 values).");
        return 1;
    }

    // Prepare buffer
    size_t buffer_size = num_chips * kPacketBytes;
    uint8_t* tx_buffer = (uint8_t*)heap_caps_malloc(buffer_size, MALLOC_CAP_DMA);
    if (!tx_buffer) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffer");
        return 1;
    }
    std::memset(tx_buffer, 0, buffer_size);

    // Fill packets
    // Input is linear list of channel values. We need to group them into 12 per chip.
    // Chip 0 gets values 0-11, Chip 1 gets 12-23, etc.
    // HOWEVER, the LAST chip in the chain must be sent FIRST.
    // So we iterate chips in reverse order: (num_chips-1) down to 0.
    
    // Create a zero-filled channel buffer for partial chips
    std::vector<uint16_t> all_channels(num_chips * 12, 0);
    for (int i = 0; i < num_values; i++) {
        all_channels[i] = (uint16_t)tlc_args.channels->ival[i];
    }

    uint8_t packet[kPacketBytes];
    int tx_offset = 0;

    // Build stream: Chip N, Chip N-1 ... Chip 0
    for (int chip_idx = num_chips - 1; chip_idx >= 0; chip_idx--) {
        // Get pointer to the 12 channels for this chip
        const uint16_t* chip_channels = &all_channels[chip_idx * 12];
        
        build_packet(packet, chip_channels);
        log_channels_hex(chip_idx, chip_channels);
        log_packet_hex(chip_idx, packet);
        std::memcpy(tx_buffer + tx_offset, packet, kPacketBytes);
        tx_offset += kPacketBytes;
    }

    // Transmit
    spi_transaction_t t = {};
    t.length = buffer_size * 8; // Bits
    t.tx_buffer = tx_buffer;

    ESP_LOGI(TAG, "TX stream (all chips, MSB-first order):");
    esp_log_buffer_hex(TAG, tx_buffer, buffer_size);
    ESP_LOGI(TAG,
             "Reason: shift register expects MSB-first, packet order last chip to first chip. "
             "Result: channels assigned directly per 12 values and latched after clock idle.");

    esp_err_t err = spi_device_transmit(spi_handle, &t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI transmit failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Sent %d bytes to %d chips", (int)buffer_size, num_chips);
    }

    // Wait for latch
    esp_rom_delay_us(200);

    heap_caps_free(tx_buffer);
    return (err == ESP_OK) ? 0 : 1;
}

void register_tlc59711(void) {
    tlc_args.channels = arg_intn(NULL, NULL, "<val>", 0, 60, "Channel values (0-65535), up to 60 values (5 chips)");
    tlc_args.init = arg_lit0(NULL, "init", "Initialize hardware pins and SPI");
    tlc_args.end = arg_end(20);

    const esp_console_cmd_t cmd = {
        .command = "tlc59711",
        .help = "Control TLC59711 LED drivers via SPI. Provide 12 values per chip.",
        .hint = NULL,
        .func = &tlc59711_cmd,
        .argtable = &tlc_args
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
}
