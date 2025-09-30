#include "lmp91000_sensor.h"
#include "i2c_master_ext.h"
#include "esp_log.h"

static const char *TAG = "LMP91000";

// Helper to write a single register
bool LMP91000Sensor::write_reg(uint8_t reg, uint8_t val) {
    if (_dev_handle == nullptr) return false;
    uint8_t buf[2] = { reg, val };
    esp_err_t ret = i2c_master_transmit(_dev_handle, buf, sizeof(buf), 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
        return false;
    }
    ESP_LOGD(TAG, "Wrote 0x%02X to reg 0x%02X", val, reg);
    return true;
}

// Helper to read a single register
bool LMP91000Sensor::read_reg(uint8_t reg, uint8_t &val) {
    if (_dev_handle == nullptr) return false;
    uint8_t rx_buf[1];
    esp_err_t ret = i2c_master_transmit_receive(_dev_handle, &reg, 1, rx_buf, 1, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
        return false;
    }
    val = rx_buf[0];
    ESP_LOGD(TAG, "Read 0x%02X from reg 0x%02X", val, reg);
    return true;
}

bool LMP91000Sensor::init(i2c_master_bus_handle_t bus_handle) {
    if (_initialized) return true;
    if (bus_handle == nullptr) return false;
    _bus_handle = bus_handle;

    // Add device on the bus
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = _i2c_addr,
        .scl_speed_hz    = 400000,
    };
    esp_err_t ret = i2c_master_bus_add_device(_bus_handle, &dev_cfg, &_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add LMP91000@0x%02X: %s", _i2c_addr, esp_err_to_name(ret));
        return false;
    }

    // Configure for SPEC 110-1xx CO cell:
    // - Zero electrode bias (0%) - SPEC CO sensors typically operate at 0V bias
    // - Internal reference at mid-rail (INT_Z = 50% of Vdd)
    // - 3-lead amperometric mode, FET open
    // - TIA gain: internal RTIA = 350kΩ
    // - RLOAD: 10Ω for stability

    // LMP91000 Register Encoding (per TI datasheet):
    //
    // TIACN (0x10): TIA Control (per TI datasheet)
    //   [7:5] RESERVED
    //   [4:2] TIA_GAIN: Transimpedance amplifier gain
    //         000 = External resistor
    //         001 = 2.75kΩ
    //         010 = 3.5kΩ
    //         011 = 7kΩ
    //         100 = 14kΩ
    //         101 = 35kΩ
    //         110 = 120kΩ  <-- We want this (closest to 100k spec)
    //         111 = 350kΩ
    //   [1:0] RLOAD: Load resistance
    //         00 = 10Ω     <-- We want this
    //         01 = 33Ω
    //         10 = 50Ω
    //         11 = 100Ω
    //

    // REFCN (0x11): Reference Control (per TI datasheet)
    //   [7]   REF_SOURCE: 0=internal, 1=external  <-- We want internal (0)

    //   [6:5] INT_Z: Internal zero selection (% of supply)
    //         00 = 20%
    //         01 = 50%      <-- We want this (mid-rail)
    //         10 = 67%
    //         11 = BYPASS
    
    //   [4]   BIAS_SIGN: 0=negative, 1=positive

    //   [3:0] BIAS: Bias percentage
    //         0000 = 0%     <-- We want this (0V bias for SPEC CO)
    //         0001 = 1%
    //         ...
    //         1101 = 24%
    //
    // MODECN (0x12): Mode Control (per TI datasheet)
    //   [7]   FET_SHORT: 0=open, 1=short
    //   [6:3] RESERVED
    //   [2:0] OP_MODE: Operating mode
    //         000 = Deep sleep
    //         001 = 2-lead ground referred
    //         010 = standby
    //         011 = 3-lead amperometric  <-- We want this
    //         110 = temperature (TIA off)
    //         111 = temperature (TIA on)

    // Unlock registers before configuration
    if (!write_reg(REG_LOCK, 0x00)) return false;
    vTaskDelay(pdMS_TO_TICKS(10));

    // TIACN = 0b000_110_00 = 0x18  (TIA_GAIN=120kΩ, RLOAD=10Ω)
    const uint8_t tiacn = 0x18;
    if (!write_reg(REG_TIACN, tiacn)) return false;

    // REFCN = 0b0_01_0_0000 = 0x20 (internal ref, 50% INT_Z, negative bias sign, 0% bias)
    const uint8_t refcn = 0x20;
    if (!write_reg(REG_REFCN, refcn)) return false;

    // MODECN = 0b0_000_0_011 = 0x03 (FET open, 3-lead amperometric)
    const uint8_t modecn = 0x03;
    if (!write_reg(REG_MODECN, modecn)) return false;

    // Read back and verify the configuration
    uint8_t verify_tiacn = 0, verify_refcn = 0, verify_modecn = 0, verify_status = 0;
    bool verify_ok = true;
    
    // Read status register first
    if (read_reg(REG_STATUS, verify_status)) {
        ESP_LOGI(TAG, "STATUS=0x%02X (READY=%d, MODE_ERR=%d, CNFG_ERR=%d)", 
                 verify_status, 
                 (verify_status >> 0) & 1,  // bit 0: READY
                 (verify_status >> 1) & 1,  // bit 1: MODE_ERR (if applicable)
                 (verify_status >> 2) & 1); // bit 2: CNFG_ERR (if applicable)
    }
    
    if (read_reg(REG_TIACN, verify_tiacn)) {
        if (verify_tiacn != tiacn) {
            ESP_LOGW(TAG, "TIACN mismatch! Expected 0x%02X, got 0x%02X", tiacn, verify_tiacn);
            ESP_LOGW(TAG, "  TIA_GAIN bits [4:2] = %d (expected 6 for 120kΩ)", (verify_tiacn >> 2) & 0x07);
            ESP_LOGW(TAG, "  RLOAD bits [1:0] = %d (expected 0 for 10Ω)", verify_tiacn & 0x03);
            verify_ok = false;
        }
    } else {
        verify_ok = false;
    }
    
    if (read_reg(REG_REFCN, verify_refcn)) {
        if (verify_refcn != refcn) {
            ESP_LOGW(TAG, "REFCN mismatch! Expected 0x%02X, got 0x%02X", refcn, verify_refcn);
            ESP_LOGW(TAG, "  REF_SOURCE bit [7] = %d (expected 0 for internal)", (verify_refcn >> 7) & 1);
            ESP_LOGW(TAG, "  INT_Z bits [6:5] = %d (expected 1 for 50%%)", (verify_refcn >> 5) & 0x03);
            ESP_LOGW(TAG, "  BIAS_SIGN bit [4] = %d", (verify_refcn >> 4) & 1);
            ESP_LOGW(TAG, "  BIAS bits [3:0] = %d", verify_refcn & 0x0F);
            verify_ok = false;
        }
    } else {
        verify_ok = false;
    }
    
    if (read_reg(REG_MODECN, verify_modecn)) {
        if (verify_modecn != modecn) {
            ESP_LOGW(TAG, "MODECN mismatch! Expected 0x%02X, got 0x%02X", modecn, verify_modecn);
            ESP_LOGW(TAG, "  FET_SHORT bit [7] = %d", (verify_modecn >> 7) & 1);
            ESP_LOGW(TAG, "  OP_MODE bits [2:0] = %d (expected 3 for 3-lead)", verify_modecn & 0x07);
            verify_ok = false;
        }
    } else {
        verify_ok = false;
    }

    if (verify_ok) {
        ESP_LOGI(TAG, "LMP91000 configuration verified successfully!");
        ESP_LOGI(TAG, "  TIACN=0x%02X (120kΩ TIA, 10Ω load)", verify_tiacn);
        ESP_LOGI(TAG, "  REFCN=0x%02X (0%% bias, 50%% INT_Z, internal)", verify_refcn);
        ESP_LOGI(TAG, "  MODECN=0x%02X (3-lead amperometric)", verify_modecn);
    } else {
        ESP_LOGE(TAG, "LMP91000 configuration verification FAILED!");
        ESP_LOGE(TAG, "Check: Is sensor connected? Are WE/RE/CE pins correct?");
    }

    // Lock the configuration registers (optional, but good practice)
    write_reg(REG_LOCK, 0x01);

    ESP_LOGI(TAG, "Configured LMP91000 for SPEC CO cell (addr=0x%02X)", _i2c_addr);
    _initialized = true;
    return true;
}


