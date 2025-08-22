#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
JS_DIR="$ROOT_DIR/js"
BUILD_DIR="$JS_DIR/dist"
FSDATA_DIR="$ROOT_DIR/fsdata"
FLASHER_JSON="$ROOT_DIR/build/flasher_args.json"

# Optional: allow passing -p/--port or PORT env; allow --baud and --no-stub
PORT="${PORT:-${IDF_PORT:-}}"
BAUD="${BAUD:-460800}"
USE_STUB=1
if [[ "${1:-}" == "-p" || "${1:-}" == "--port" ]]; then
  shift
  PORT="$1"
  shift || true
fi
if [[ "${1:-}" == "--baud" ]]; then
  shift
  BAUD="$1"
  shift || true
fi
if [[ "${1:-}" == "--no-stub" ]]; then
  shift
  USE_STUB=0
fi

echo "[1/5] Building Vite app (single file)"
pushd "$JS_DIR" >/dev/null
npm ci
npm run build
popd >/dev/null

INDEX_SRC="$BUILD_DIR/index.html"
if [[ ! -f "$INDEX_SRC" ]]; then
  echo "Error: $INDEX_SRC not found. Build failed?" >&2
  exit 1
fi

echo "[2/5] Preparing fsdata directory"
rm -rf "$FSDATA_DIR"
mkdir -p "$FSDATA_DIR"

echo "[3/5] Compressing index.html -> index.html.gz"
gzip -c -9 "$INDEX_SRC" > "$FSDATA_DIR/index.html.gz"

# Always (re)build so storage.bin includes latest fsdata contents
echo "[4/5] Building project to generate storage.bin (filesystem image)"
idf.py build >/dev/null

if [[ ! -f "$FLASHER_JSON" ]]; then
  echo "Error: flasher_args.json not found at $FLASHER_JSON. Run 'idf.py build' first." >&2
  exit 1
fi

OFFSET_HEX=$(python3 - "$FLASHER_JSON" <<'PY'
import json,sys
p=sys.argv[1]
with open(p) as f:
    j=json.load(f)
# Prefer 'storage' key if present
if isinstance(j, dict) and 'storage' in j and 'offset' in j['storage']:
    print(j['storage']['offset'])
    sys.exit(0)
# fallback: find flash_files entry for storage.bin
for off, fname in j.get('flash_files',{}).items():
    if fname=='storage.bin':
        print(off)
        sys.exit(0)
print('')
PY
)

if [[ -z "$OFFSET_HEX" ]]; then
  echo "Error: could not determine storage offset from flasher_args.json" >&2
  exit 1
fi

IMG_PATH="$ROOT_DIR/build/storage.bin"

echo "[5/5] Flashing FS image via esptool.py at offset $OFFSET_HEX"

# Auto-detect a likely serial port if not provided
if [[ -z "$PORT" ]]; then
  # Prefer usbserial and ignore debug-console
  CANDIDATES=(/dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART* /dev/cu.usbmodem* /dev/cu.wchusbserial*)
  for c in "${CANDIDATES[@]}"; do
    for d in $c; do
      if [[ -e "$d" && "$d" != *"debug"* ]]; then
        PORT="$d"
        break 2
      fi
    done
  done
fi

FLASH_CMD=(esptool.py --chip esp32s3)
if [[ -n "$PORT" ]]; then
  echo "Using port: $PORT"
  FLASH_CMD+=(--port "$PORT")
else
  echo "No serial port specified or detected; trying esptool auto-detect"
fi
FLASH_CMD+=(--baud "$BAUD")
if [[ "$USE_STUB" -eq 0 ]]; then
  FLASH_CMD+=(--no-stub)
fi
FLASH_CMD+=(write_flash "$OFFSET_HEX" "$IMG_PATH")

set +e
"${FLASH_CMD[@]}"
RC=$?
if [[ $RC -ne 0 && "$USE_STUB" -ne 0 ]]; then
  echo "Flash failed with stub at $BAUD, retrying without stub at 115200..."
  esptool.py --chip esp32s3 ${PORT:+--port "$PORT"} --baud 115200 --no-stub write_flash "$OFFSET_HEX" "$IMG_PATH"
  RC=$?
fi
set -e

exit $RC

echo "Done."


