#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

namespace config {
class ConfigurationManager;
}

namespace ble {

// Starts the BLE scanner's control task. Bluetooth itself (NimBLE, observer only) runs only on
// boards whose ble config module asks for scanning, and scans only while MQTT is connected. Its
// controller takes about 30 KB of internal RAM; on boards set up to scan it starts here, so call
// this after the modules that must not lose that RAM to it (audio) and before OTA.
//
// Each ble.interval seconds it publishes:
// - for each tracked device heard: sensor/<mac>/metrics/ble_rssi, a metric reading whose value
//   is the mean RSSI (dBm) over the window, tagged with the device's name and ble_addr, plus
//   min, max and count fields
// - when ble.scan is on: sensor/<mac>/ble/devices, every device heard in the window
esp_err_t init_scanner(config::ConfigurationManager& cfg);

// Shuts Bluetooth down, freeing the controller's internal RAM, and keeps it down until resume().
// For OTA checks and downloads, and audio playback: their TLS connections need that RAM (about
// 11 KB of it DMA-capable per handshake), and they all want the radio. Calls nest: Bluetooth
// stays down until every suspend() has had its resume(). Waits up to timeout for Bluetooth to
// stop; returns false if it didn't (the call still counts, so resume() it).
bool suspend(TickType_t timeout);

// Ends one suspend(). Bluetooth starts again once none is left, if the ble config asks for it.
void resume();

} // namespace ble
