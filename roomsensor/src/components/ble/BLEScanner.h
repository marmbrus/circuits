#pragma once

#include "esp_err.h"

namespace config {
class ConfigurationManager;
}

namespace ble {

// Starts the BLE scanner's control task. Bluetooth itself (NimBLE, observer only) starts the
// first time the ble config module asks for scanning, so boards that never scan never start it.
//
// Each ble.interval seconds it publishes:
// - for each tracked device heard: sensor/<mac>/metrics/ble_rssi, a metric reading whose value
//   is the mean RSSI (dBm) over the window, tagged with the device's name and ble_addr, plus
//   min, max and count fields
// - when ble.scan is on: sensor/<mac>/ble/devices, every device heard in the window
esp_err_t init_scanner(config::ConfigurationManager& cfg);

} // namespace ble
