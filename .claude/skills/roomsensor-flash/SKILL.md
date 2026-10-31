---
name: roomsensor-flash
description: How to build, flash, and provision roomsensor boards (ESP32-S3 firmware in roomsensor/src, also used by the lamp, LED, flipdot and window-sensor boards). Covers the files involved and their formats (partitions.csv, build images and offsets, fsdata/LittleFS, credentials.json and config_*.json), util/provision.py and its options, per-board certificates signed by a YubiKey (--device-cert, the device_cert NVS namespace), update_js.sh, firmware-only esptool writes, OTA from updates.acgt.net via deploy_ota.sh, and the serial console's NVS and device_cert commands. Use it whenever the task involves flashing, reflashing, erasing, provisioning, giving a board its certificate, setting Wi-Fi or MQTT settings on a board, pushing an OTA update, or a board that boots the wrong firmware, has lost its web UI, or can't reach MQTT or OTA, even if "flash" isn't said.
---

# Flashing roomsensor boards

Everything runs from `roomsensor/src`. The target is an ESP32-S3 with 16 MB flash, built with ESP-IDF v5.3.2 plus ESP-ADF v2.7 (CMake fails without `ADF_PATH`).

## Toolchain

- **Docker (preferred):** `../../docker/esp-idf/idf <idf.py args>`. It also takes `idf run <cmd>` and `idf shell`. See `docker/esp-idf/README.md`. If `build/` came from a host IDF, run `idf fullclean` once.
- **macOS:** Docker can't reach USB serial ports, and the wrapper's `stat -c` is GNU-only, so `idf -p … flash` fails. Build in Docker, then flash from the host with `esptool` (`pip install esptool`). Ports look like `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`.
- **Host IDF:** `util/provision.py`'s flash step hard-codes `. ~/esp/v5.3/esp-idf/export.sh && idf.py erase-flash flash`. Without that install, pass `--skip-flash`.
- **Python utilities:** `cd util && python3 -m venv .venv && . .venv/bin/activate && pip install -e .` (installs pyserial, plus `cryptography` and `yubikey-manager` for `--device-cert`).

## Files

| File | Format | Role |
|---|---|---|
| `partitions.csv` | ESP-IDF partition CSV: `Name, Type, SubType, Offset, Size, Flags` | Flash layout (table below) |
| `sdkconfig.defaults` | Kconfig lines (`CONFIG_X=y`) | Seed for the generated, gitignored `sdkconfig`. Delete `sdkconfig` and run `set-target esp32s3` to pick up changes. |
| `build/*.bin` | Raw images | Written by `idf.py flash` / esptool |
| `build/flash_args` | esptool args file (`--flash_mode …` then `offset file` pairs) | `esptool.py --chip esp32s3 -p PORT write_flash @flash_args`, run from `build/` |
| `build/flasher_args.json` | JSON: `flash_files` maps offset→file, plus a per-image `{offset, file}` | Read by `update_js.sh` to find the `storage` offset |
| `flasher_args.json` (project root) | — | Stale leftover from another project (2MB, `surprise.bin`). Ignore it. |
| `fsdata/` | Directory packed into a LittleFS image (`build/storage.bin`) | `index.html.gz` (gzipped single-file Vite build of `js/`), `webapp.json` and `firmware.json`. Each JSON is `{local_version, local_git_describe, local_build_timestamp, local_build_timestamp_epoch}`, regenerated on every build. Gitignored. |
| `credentials.json` | Config JSON (below) | Gitignored. `provision.py` applies it before any other config. It normally holds `wifi.ssid`, `wifi.password` and `wifi.mqtt_broker`. |
| `config_<board>.json` | Config JSON | Board-type settings: GPIOs, LEDs, sensors, `device.type` |
| `certs/letsencrypt_gen_y_roots.pem` | PEM bundle | Added to ESP-IDF's CA bundle (`CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH` in `sdkconfig.defaults`) so OTA keeps trusting `updates.acgt.net`'s Let's Encrypt chain |

### Flash layout

| Partition | Offset | Size | Written by |
|---|---|---|---|
| bootloader | `0x0` | | `bootloader/bootloader.bin` |
| partition table | `0x8000` | | `partition_table/partition-table.bin` |
| `nvs` | `0x9000` | 24K | Holds the settings and the board certificate. Never written by `flash`. Only the console, `provision.py`, MQTT config, or `erase-flash` (wipes it) touch it. |
| `phy_init` | `0xf000` | 4K | |
| `otadata` | `0x10000` | 8K | `ota_data_initial.bin` (resets boot to `factory`) |
| `factory` | `0x20000` | 4000K | `sensorv2.bin` |
| `ota_0` / `ota_1` | `0x410000` / `0x800000` | 4000K each | OTA only |
| `coredump` | `0xbe8000` | 128K | |
| `storage` (LittleFS) | `0xc08000` | 4000K | `storage.bin` |

### Config JSON format

The top-level keys are NVS namespaces, one per config module: `device`, `wifi`, `tags`, `led1`–`led4`, `a2d1`–`a2d4`, `io1`–`io8`, `i2c`, `motion`, `audio`, `life`. Each value is an object of key → value:

```json
{
  "wifi": { "ssid": "MyIoT", "password": "…", "mqtt_broker": "mqtts://mqtt.acgt.net:8883", "statusGPIO": 38 },
  "tags": { "area": "house", "room": "office" },
  "led1": { "dataGPIO": 3, "chip": "WS2812", "num_rows": 6, "num_columns": 6 }
}
```

- `provision.py` maps JSON types to NVS types like this: `true`/`false` → `u8`, integer → `i32` (`i64` if it doesn't fit), float → `str`, string → `str`.
- At boot each key is read with the type its module declares (`components/configuration/<Module>Config.cpp`, the `descriptors_` list). A key stored with a different type is silently ignored, so match the JSON type to the descriptor: GPIOs as numbers, `i2c` address → driver name as strings.
- String values go to the console unquoted, so they can't contain spaces. NVS namespace and key names are at most 15 characters.
- An `mqtts://` broker uses the board certificate, and its host name must be one on the broker's certificate: `mqtt.acgt.net`, `smll` or `10.10.0.197`. With `mqtt://` the certificate isn't used for MQTT.

## Ways to flash

**1. Blank board, end to end:** `python util/provision.py [config.json ...] [--port PORT] [--skip-flash | --program] [--device-cert [--yubikey SERIAL] [--mqtt-ca FILE]]`
- It validates every config file, and the YubiKey when `--device-cert` is given. Then it runs `idf.py erase-flash flash` (unless `--skip-flash`; `--program` is the same flag).
- Then it resets the board over RTS/DTR and waits for `Console initialized` and `Startup sequence complete`.
- It applies `credentials.json`, then each config file in order, then the certificate (see below), then sends `restart`.
- If `--port` is missing it picks the most USB-looking serial port.

**2. Build and flash all images, keeping NVS:**
```bash
../../docker/esp-idf/idf build
../../docker/esp-idf/idf -p /dev/ttyACM0 flash                    # Linux
(cd build && esptool.py --chip esp32s3 -p PORT write_flash @flash_args)  # macOS
```
This rewrites `otadata`, so the board boots `factory`. It also overwrites `storage` with whatever is in `fsdata/` (see the web UI caveat below). Follow it with `provision.py --skip-flash` if NVS is empty.

**3. Firmware only:** write the OTA record along with the app. A board that has taken an OTA update boots from `ota_0`/`ota_1` and would ignore a new `factory` image.
```bash
esptool.py --chip esp32s3 -p PORT write_flash 0x10000 build/ota_data_initial.bin 0x20000 build/sensorv2.bin
```

**4. Web UI only:** `./update_js.sh [-p PORT] [--baud 460800] [--no-stub]`
- The flags are parsed only in that order.
- It runs `npm ci && npm run build` in `js/` and gzips `index.html` into a fresh `fsdata/`.
- It runs `idf.py build` to repack `storage.bin`, then writes it at the `storage` offset.
- If the stub fails, it retries at 115200 without the stub.
- It needs `idf.py` and `esptool.py` on PATH: use `idf run ./update_js.sh -p PORT` on Linux, or a host IDF.

**5. OTA, no cable:** `./deploy_ota.sh [prod]`
- It builds and uploads `firmware-<hash>.bin`, `index-<hash>.html.gz` and `manifest-dev.json` (or `manifest.json` for `prod`, which needs a clean tree) to `smll:/home/brayden/updates`. Uploading needs the `brayden` group.
- Brayden's nginx serves that directory as `https://updates.acgt.net`, and only to boards presenting a house certificate.
- Boards with `wifi.channel` set read `manifest-<channel>.json`; boards without it read `manifest.json`. Leave `channel` unset for prod: `channel=prod` would look for `manifest-prod.json`, which nothing writes.
- `force_ota [hash]` on the console forces an update now.
- Run it on the host, not in Docker: it needs your SSH keys for `scp` and BSD `stat`.

## Board certificate

Each board has its own client certificate. The MQTT broker's TLS listener (8883) needs it, and so does `updates.acgt.net`, which serves no OTA updates to a board without one.

```bash
python util/provision.py --program --device-cert [--yubikey SERIAL] [--mqtt-ca FILE]
```

- **Needs:** a house YubiKey plugged in (the CA key in PIV slot 9a), and the `util/` package installed.
- **What it does:**
  - It reads the board's name from `device_cert status` and makes an EC P-256 key on the host.
  - The YubiKey signs a certificate for it: `CN=ESP_<mac>`, `O=acgt.net`, client authentication only, valid five years.
  - It writes the key, certificate and CA to the board. No copy of the key is kept, so re-provisioning makes a new key.
- **`--yubikey SERIAL`:** which YubiKey to sign with, when more than one is plugged in.
- **`--mqtt-ca FILE`:** a PEM bundle of the authorities the board checks the broker's certificate against. The default is the YubiKey's own CA certificate. To trust every house authority, use `~/.brayden/config/security/mqtt/broker/ca.pem` from smll. `updates.acgt.net` is checked with ESP-IDF's bundle instead.
- **Flashing too:** drop `--program` to flash first, and add config files to apply them in the same run. The board uses the certificate from its next boot. Brayden creates its broker account (limited to `sensor/<mac>/#`) the first time it connects.

**Storage:** NVS namespace `device_cert`, as blobs `cert`, `key` and `ca`, each a PEM without its trailing NUL.

**On the wire:** a console line holds 255 characters, so the PEMs go over in base64 pieces:
1. `device_cert begin`
2. `device_cert add <cert|key|ca> <base64>`, 192 characters per line
3. `device_cert commit`, which stores them only if all three parse, the key matches the certificate, and the certificate's CN is this board's name

Each step answers with a line starting `DEVICE CERT ok` or `DEVICE CERT error`.

**Logs:** the serial output of that run contains the private key, so don't paste or save it.

## Serial console

115200 baud: `python -m serial.tools.miniterm PORT 115200` or `idf.py -p PORT monitor`.

| Command | Use |
|---|---|
| `nvs_namespace <ns>` | Select a namespace. The default is `storage`, so always set it first. |
| `nvs_set <key> <type> -v <value>` | Types: `i8 u8 i16 u16 i32 u32 i64 u64 str blob` (blob as hex) |
| `nvs_get <key> <type>` / `nvs_erase <key>` / `nvs_erase_namespace <ns>` | Read or remove values |
| `nvs_list nvs -n <ns>` | Show what's stored |
| `device_cert status` / `device_cert erase` | Show the board's name (`ESP_<mac>`) and its certificate, or remove it |
| `restart`, `version`, `force_ota [hash]` | NVS changes take effect after `restart` |

After the board connects, it publishes its effective config on MQTT at `sensor/<mac>/config/current`.

## Things that bite

- **`erase-flash` wipes NVS,** including the Wi-Fi settings, tags, board config and the board certificate. Only use it on a blank board or when you have the JSON to replay, and add `--device-cert`.
- **A board without a certificate can't update itself.** `updates.acgt.net` refuses it, and an `mqtts://` broker refuses the connection. The boot log says which.
- **A full flash can leave the board without its web UI.** `idf build` packs whatever is in `fsdata/`, and only `update_js.sh` puts `index.html.gz` there, so a fresh checkout has none. The build also stamps `webapp.json` with the build time, so OTA treats the missing UI as newer than the server's and won't fetch it. Run `update_js.sh` afterwards, or use path 3.
- **If a flash appears to do nothing,** the board is booting an OTA slot. Write `ota_data_initial.bin` too.
