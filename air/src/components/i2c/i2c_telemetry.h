#pragma once

#include "i2c_sensor.h"

// Publish retained JSON to sensor/$mac/device/i2c describing recognized sensors
// recognized[i] indicates whether sensors[i] was detected on the bus during scan
// Also include list of unrecognized device addresses (raw 7-bit addresses)
void publish_i2c_topology(const I2CSensor* const* sensors,
	const bool* recognized,
	int num_sensors,
	const uint8_t* unrecognized_addrs,
	int num_unrecognized);


