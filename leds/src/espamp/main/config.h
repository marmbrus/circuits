#ifndef __CONFIG_H__
#define __CONFIG_H__

// I2C Master configuration
#define I2C_MASTER_NUM           I2C_NUM_0      // I2C port number 0
#define I2C_MASTER_SDA_IO        10             // GPIO number for I2C SDA signal
#define I2C_MASTER_SCL_IO        9              // GPIO number for I2C SCL signal
#define I2C_MASTER_FREQ_HZ       400000         // I2C master clock frequency (400kHz)
#define I2C_CLK_SRC_DEFAULT      I2C_CLK_SRC_DEFAULT  // Default I2C clock source

#endif // __CONFIG_H__