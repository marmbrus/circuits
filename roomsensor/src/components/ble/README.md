# Bluetooth LE scanning

The board can listen for Bluetooth LE advertisements and report what it hears over MQTT. It
does two things, each switched on through the `ble` config module:

- **Discovery** (`ble/scan`): every device heard, published each interval, to find the devices
  worth tracking (a Tile tracker's address, say).
- **Tracking** (`ble/track`): for each named device, a signal strength reading each interval
  it was heard. Several boards hearing the same tag give a rough position: it's nearest the
  board that hears it loudest.

Bluetooth stays off until one of them is set: boards that never scan don't start the controller
or the NimBLE host, use no airtime, and keep Wi-Fi out of modem sleep. Once it has started it
stays up until the board restarts; clearing both only stops the scan.

`BLEScanner.cpp` runs NimBLE as an observer and publishes; `../ble_logic` parses addresses,
track lists and advertisements and keeps the per-window statistics, with host unit tests
(`../ble_logic/host_test/run.sh`, which needs only `g++`).

## Configuration

Set like any other module: publish to `sensor/<mac>/config/ble/<key>` (not retained). Every
key is persisted in NVS.

| Key | Default | Meaning |
| :--- | :--- | :--- |
| `scan` | `false` | Discovery: publish every device heard to `sensor/<mac>/ble/devices`. Also makes the scan active, so devices answer with their names. |
| `track` | empty | Devices to report, `aa:bb:cc:dd:ee:ff=Mittens,11:22:33:44:55:66=Tom`. Addresses may also be written `aabbccddeeff` or with dashes; names are optional (default the address), up to 31 characters, without `,` `;` or `=`. Up to 32 devices. An invalid list is rejected whole. Empty clears it. |
| `interval` | `10` | Seconds per report, 1 to 3600. RSSI is averaged over each interval. |
| `duty` | `50` | Percent of the time the radio listens (of every 100 ms), 1 to 100. Wi-Fi gets the rest. |
| `minRssi` | unset | Discovery leaves out devices never heard stronger than this, in dBm (e.g. `-85`). Tracking ignores it. |

```sh
mosquitto_pub -h <broker> -t sensor/<mac>/config/ble/scan -m true
mosquitto_pub -h <broker> -t sensor/<mac>/config/ble/track -m 'e1:2b:03:4c:5d:6f=Mittens,c4:aa:01:02:03:04=Tom'
```

`sensor/<mac>/config/current` shows the `ble` object once anything in it is set, with the
track list written out in full.

## What it publishes

### `sensor/<mac>/metrics/ble_rssi` (tracked devices)

One message per tracked device heard during the interval, nothing for one that wasn't. It's a
regular metric reading, so the server records it in InfluxDB (measurement `ble_rssi`) and the
dashboard shows it with the board's other readings:

```json
{"metric":"ble_rssi","value":-71.4,"ts":"2026-10-29T21:14:03.120Z",
 "tags":{"area":"Outside","room":"FrontGate","id":"Gate","mac":"1020BA756E44","sensor":"FrontGate-Gate",
         "name":"Mittens","ble_addr":"e1:2b:03:4c:5d:6f","type":"ble"},
 "min":-80,"max":-66,"count":7,"window_s":10.0,"tx_power":-12}
```

`value` is the mean RSSI in dBm over the window; `min`, `max` and `count` (advertisements
heard) describe its spread, and `tx_power` is there when the device advertises it.

### `sensor/<mac>/ble/devices` (discovery)

One message per interval while `scan` is on, listing up to 64 devices, loudest first:

```json
{"ts":"2026-10-29T21:14:03.120Z","window_s":10.0,"heard":23,"listed":2,"min_rssi":-90,
 "devices":[
   {"addr":"e1:2b:03:4c:5d:6f","addr_type":"random_static","rssi":-58.5,"min":-61,"max":-55,"count":9,
    "uuids":["feed"],"tile":true,"tracked":"Mittens"},
   {"addr":"5a:11:22:33:44:55","addr_type":"random_private","rssi":-77.0,"min":-79,"max":-75,"count":4,
    "name":"Pixel 9","mfr":"00e0","tx_power":7}]}
```

- `addr_type` says whether the address will still mean this device tomorrow: `public` and
  `random_static` stay put; `random_private` and `random_nonresolvable` change every few
  minutes (phones, AirTags), so they can't be tracked by address.
- `uuids` are the 16-bit service UUIDs advertised; `mfr` the manufacturer's company ID (hex);
  `tile` marks Tile, Inc.'s UUIDs (`feed`, `feec`).
- `tracked` is the device's name when it's on the track list.
- `heard` counts every device heard, including those left out by `minRssi` or the 64 limit
  (and any past the 128 the board can keep per window).

Both topics are published at QoS 0 without retain, and only while MQTT is connected; a window
that closes while it's down is dropped.

## Finding and tracking a Tile

1. Turn on discovery on a board near the tag: `ble/scan` = `true`.
2. Look for `"tile": true` entries on `sensor/<mac>/ble/devices`. Move the tag (or carry it
   away) and watch which address's RSSI follows it. Tiles advertise a fixed address today
   (`random_static`), which is what makes this work; Life360 has said Tiles will move to rotating
   addresses, which would stop it.
3. Put it on the track list of every board that should report it: `ble/track` =
   `<addr>=<name>`, then turn discovery back off.

A Tile connected to its owner's phone may advertise less often or not at all; discovery shows
whether the tag is heard while the phone is nearby.

## Radio and memory

- Wi-Fi and Bluetooth share the ESP32-S3's radio through software coexistence. Wi-Fi has to
  run in modem sleep then (IDF aborts if power save is off while Bluetooth runs), so a board
  with BLE configured boots with `WIFI_PS_MIN_MODEM` instead of `WIFI_PS_NONE`, and the scanner
  switches it before starting Bluetooth. Expect a little more Wi-Fi latency, and less
  throughput the higher `duty` is; lower `duty` on boards that stream audio.
- Building Bluetooth in costs every board about 180 KB of flash and 25 KB of internal RAM
  (static code and data, measured with ESP-IDF v5.3.2), whether or not it scans. Once started,
  NimBLE's host allocates from PSRAM (`CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`) and its task
  stack is in PSRAM, as is the discovery table (about 10 KB, only while `scan` is on).
- The controller takes about 30 KB of **internal** RAM at runtime, and internal RAM is what a
  running board is short of: on a lamp with audio (measured on `ESP_9c139ebc0eb0`), Wi-Fi, MQTT,
  the console, the HTTP server, the I2S DMA buffers and the OTA task left about 14 KB. To make
  room:
  - Bluetooth starts early in boot, before Wi-Fi and audio, on boards whose `ble` module is
    already set up. Turning `scan` or `track` on later also works now, but it's the start at
    boot that's sure of the memory.
  - The stacks of tasks that never touch flash live in PSRAM: `ble-ctrl`, `nimble_host`,
    `netlog`, `metrics_task`, `status_led_task` and `audio-mic`, and so does netlog's queue
    (about 13 KB). A task that reads or writes flash (NVS, partitions, the core dump, LittleFS)
    must keep an internal stack: IDF asserts otherwise.
  - TLS uses software AES (`CONFIG_MBEDTLS_HARDWARE_AES` off). Hardware AES needs a DMA
    bounce buffer in internal RAM for the TLS buffers, which are in PSRAM, and with Bluetooth
    running MQTT and the OTA check ran out of it and dropped their connections.
  - Audio no longer aborts when its I2S DMA buffers can't be allocated; the board comes up
    without audio instead of rebooting in a loop.
  With all that, the audio lamp above boots with about 18 KB of internal RAM free while
  scanning (and 45 to 49 KB without Bluetooth), against 14 KB before Bluetooth was added.
- Bluetooth is enabled in `sdkconfig.defaults`, which only applies when `sdkconfig` is
  generated. An existing build directory keeps Bluetooth off until `sdkconfig` is regenerated
  (delete it, or run `idf.py set-target esp32s3`). Firmware built without Bluetooth logs an
  error if `ble` is configured and otherwise runs as before.
