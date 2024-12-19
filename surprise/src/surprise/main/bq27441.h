#ifndef BQ27441_H
#define BQ27441_H

#include "esp_err.h"
#include "i2c_master_ext.h"

#define BQ27441_I2C_ADDRESS 0x55 // Default I2C address for BQ27441

// Register addresses for various parameters
#define BQ27441_COMMAND_TEMP            0x02 // Temperature()
#define BQ27441_COMMAND_VOLTAGE         0x04 // Voltage()
#define BQ27441_COMMAND_FLAGS           0x06 // Flags()
#define BQ27441_COMMAND_NOM_CAPACITY    0x08 // NominalAvailableCapacity()
#define BQ27441_COMMAND_AVAIL_CAPACITY  0x0A // FullAvailableCapacity()
#define BQ27441_COMMAND_REM_CAPACITY    0x0C // RemainingCapacity()
#define BQ27441_COMMAND_FULL_CAPACITY   0x0E // FullChargeCapacity()
#define BQ27441_COMMAND_AVG_CURRENT     0x10 // AverageCurrent()
#define BQ27441_COMMAND_STDBY_CURRENT   0x12 // StandbyCurrent()
#define BQ27441_COMMAND_MAX_CURRENT     0x14 // MaxLoadCurrent()
#define BQ27441_COMMAND_AVG_POWER       0x18 // AveragePower()
#define BQ27441_COMMAND_SOC             0x1C // StateOfCharge()
#define BQ27441_COMMAND_INT_TEMP        0x1E // InternalTemperature()
#define BQ27441_COMMAND_SOH             0x20 // StateOfHealth()
#define BQ27441_COMMAND_REM_CAP_UNFL    0x28 // RemainingCapacityUnfiltered()
#define BQ27441_COMMAND_REM_CAP_FIL     0x2A // RemainingCapacityFiltered()
#define BQ27441_COMMAND_FULL_CAP_UNFL   0x2C // FullChargeCapacityUnfiltered()
#define BQ27441_COMMAND_FULL_CAP_FIL    0x2E // FullChargeCapacityFiltered()
#define BQ27441_COMMAND_SOC_UNFL        0x30 // StateOfChargeUnfiltered()

typedef struct {
    uint16_t temperature;
    uint16_t voltage;
    uint16_t flags;
    uint16_t nominal_capacity;
    uint16_t available_capacity;
    uint16_t remaining_capacity;
    uint16_t full_capacity;
    int16_t average_current;
    int16_t standby_current;
    int16_t max_current;
    int16_t average_power;
    uint16_t soc;
    uint16_t internal_temperature;
    uint16_t soh;
    uint16_t remaining_capacity_unfiltered;
    uint16_t remaining_capacity_filtered;
    uint16_t full_capacity_unfiltered;
    uint16_t full_capacity_filtered;
    uint16_t soc_unfiltered;
} BatteryGaugeData;

void bq27441_set_i2c_handle(i2c_master_bus_handle_t handle);
esp_err_t bq27441_read_data(BatteryGaugeData *battery_data);

#endif // BQ27441_H