# ESP-IDF Docker build environment

Builds the ESP32 firmware (roomsensor, espamp, surprise) without installing
ESP-IDF on the host. The image contains:

- ESP-IDF v5.3.2 (`espressif/idf`), patched with ESP-ADF's FreeRTOS patch
- ESP-ADF v2.7 at `$ADF_PATH` (`/opt/esp/adf`), for the I2S audio parts
- Node 22 (roomsensor's web UI) and `jq` (`deploy_ota.sh`)

The image is built on first use, and rebuilt whenever this Dockerfile changes.

## Usage

Run from an ESP-IDF project directory. Arguments go to `idf.py`:

```sh
cd roomsensor/src
../../docker/esp-idf/idf set-target esp32s3
../../docker/esp-idf/idf build
../../docker/esp-idf/idf -p /dev/ttyACM0 flash monitor
../../docker/esp-idf/idf run ./update_js.sh -p /dev/ttyACM0   # any command
../../docker/esp-idf/idf shell                                # interactive shell
```

Tip: `alias idf=~/circuits/docker/esp-idf/idf`.

- The repo is mounted at its host path, and the container runs as your uid, so
  `build/` lands in the project directory and is owned by you.
- The serial port named with `-p`/`--port` (or `PORT`/`ESPPORT`) is passed
  through to the container.
- Component-manager downloads and ccache are kept in the `circuits-esp-cache`
  Docker volume.
- If a `build/` directory came from a host-installed IDF, run `idf fullclean`
  once, because the toolchain paths differ.
- Set `IDF_DOCKER_REBUILD=1` to force a rebuild, or `IDF_VERSION=v5.x.y` to
  use a different IDF. ADF's patch is selected by the IDF minor version, so
  bump `ADF_VERSION` in the Dockerfile to match.

## Using ESP-ADF in a project

Add this to the project's top-level `CMakeLists.txt`, before `project.cmake`:

```cmake
include($ENV{ADF_PATH}/CMakeLists.txt)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
```
