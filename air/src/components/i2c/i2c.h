#pragma once

/**
 * @brief Initialize the I2C subsystem
 * 
 * This function initializes the I2C bus, scans for devices, 
 * and initializes any recognized sensors. It also starts
 * a background task to poll the sensors periodically.
 * 
 * @return true If I2C subsystem was successfully initialized
 * @return false If initialization failed
 */
bool init_i2c();

/**
 * @brief Signal the I2C sensor polling task to run immediately (e.g. triggered by an interrupt)
 */
void signalSensorInterrupt(); 