## Room Sensor PCB

A compact ESP32-S3–based room sensor board integrating multiple environmental and motion sensors, addressable RGB LEDs, USB‑C connectivity, and regulated 3.3 V power.

### Core MCU & Buttons
- **MCU: ESP32‑S3‑WROOM‑1** (`U201`)
  - Dual‑core Xtensa with Wi‑Fi + BLE
  - Native USB for programming and CDC
  - Serial port header for debuging
- **Buttons** (`SW201`, `SW202`): Boot/Mode and Reset

### Sensors
- **Digital MEMS Microphone – ICS‑43434** (`U1001`) ([C5656610](https://www.lcsc.com/search?q=C5656610))
  - Interface: I2S (WS/LRCLK, SCK/BCLK, SD/DOUT)
- **3‑Axis Accelerometer – LIS2DH (LIS2DHTR)** (`U1201`) ([C155670](https://www.lcsc.com/search?q=C155670))
  - Interface: I2C
- **Environmental Sensor – BME280** (`U301`)
  - Interface: I2C (temperature, humidity, pressure)
- **Ambient Light Sensor – OPT3001** (`U2002`)  ([C90462](https://www.lcsc.com/search?q=C90462))
  - Interface: I2C (lux)
- **PIR Motion Sensor – EKMC1601112** (`U2001`)  ([C5128270](https://www.lcsc.com/search?q=C5128270))
  - Interface: Digital output to MCU GPIO

### Addressable RGB LEDs
- **36× WS2812B‑compatible RGB LEDs (1.6×1.5 mm)** (`LED1401…LED1906`) ([C41413180 lcsc](https://www.lcsc.com/search?q=C41413180))
  - Part: XL‑1615RGBC‑WS2812B‑S
  - Daisy‑chained single‑wire data from MCU; power and ground in common

Notes:
- Budget up to ~60 mA per LED at full‑white (worst case). 36 LEDs ⇒ up to ~2.16 A at the LED supply voltage

### Power
- **USB-A PCB**
  - Insert the sensor directly into a USB wall adapter with a separate cable.
- **3.3 V LDO Regulator – TPS73733DCQ** (`U501`) ([C5220270](https://www.lcsc.com/search?q=C5220270))
  - Provides regulated 3.3 V rail for MCU and sensors
- **Primary Input: USB‑C Receptacle – TYPE‑C‑31‑M‑12** (`USBC701`) ([C165948 ](https://www.lcsc.com/search?q=C165948))
  - Power input and USB data to ESP32‑S3
- **USB ESD Protection – USBLC6‑2SC6** (`U701`, `U801`)
  - ESD protection for USB D+ / D− lines

### I2C Inputs
- 5V, level-shifted internally to 3v3
- [qwiic Compatible](https://learn.sparkfun.com/tutorials/i2c/qwiic-connect-system)
- **JST‑SH 1.0 mm, 4‑pin – SM04B‑SRSS‑TB** (`CN1102`, `CN601`) ([C160390](https://www.lcsc.com/search?q=C160390))


### Onewire temperature input
- **JST‑SH 1.0 mm, 3‑pin – SM03B‑SRSS‑TB** (`U1301`) ([C160403](https://www.lcsc.com/search?q=C160403))
