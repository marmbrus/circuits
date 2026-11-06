#!/bin/bash
set -e

# Source ESP-IDF environment if IDF_PATH is set
if [ -n "$IDF_PATH" ]; then
    echo "Sourcing IDF environment from $IDF_PATH/export.sh"
    source "$IDF_PATH/export.sh"
fi

# Configuration: Brayden's nginx on smll serves REMOTE_DIR as updates.acgt.net, to boards with a
# house certificate only (brayden repo, docker/smll/auth). Uploading needs the brayden group.
SERVER="${OTA_SERVER:-smll.home}"  # [user@]host; OTA_SERVER overrides
REMOTE_DIR="/home/brayden/updates"
BASE_URL="https://updates.acgt.net"
BIN_FILE="build/sensorv2.bin"
# The version and time build_info.sh stamped into the firmware; the manifest repeats them
BUILD_INFO="build/build_info.json"

# Check if we're in a git repository
if ! git rev-parse --is-inside-work-tree > /dev/null 2>&1; then
    echo "Error: Not in a git repository"
    exit 1
fi

# Channel selection: default to dev unless 'prod' specified
CHANNEL="dev"
if [[ "$1" == "prod" ]]; then
  CHANNEL="prod"
fi

# Check for uncommitted changes only for prod
if [[ "$CHANNEL" == "prod" ]]; then
  if [[ -n $(git status --porcelain) ]]; then
      echo "Error: Git working directory is not clean. Please commit or stash your changes first."
      echo "Uncommitted changes:"
      git status --short
      exit 1
  fi
fi

# Reset sdkconfig to defaults to ensure a clean, reproducible build
echo "Resetting sdkconfig to defaults for esp32s3 target..."
rm -f sdkconfig
idf.py set-target esp32s3 > /dev/null

# Build firmware; build_info.sh stamps it and writes build/build_info.json
echo "Building project (firmware) with idf.py..."
idf.py build || { echo "Error: Build failed"; exit 1; }
echo "Firmware build successful"

if [ ! -f "$BUILD_INFO" ]; then
    echo "Error: $BUILD_INFO not found after build."
    exit 1
fi
VERSION_FOR_MANIFEST=$(jq -r '.version' "$BUILD_INFO")
BUILD_TIMESTAMP=$(jq -r '.build_timestamp_epoch' "$BUILD_INFO")
BUILD_ISO_TIME=$(jq -r '.build_timestamp' "$BUILD_INFO")
GIT_DESCRIBE=$(jq -r '.git_describe' "$BUILD_INFO")
echo "Using firmware version: $VERSION_FOR_MANIFEST ($BUILD_ISO_TIME)"

# prod only ships builds from a clean tree
if [[ "$CHANNEL" == "prod" && "$VERSION_FOR_MANIFEST" == *-dirty ]]; then
    echo "Error: the build is from a modified tree ($VERSION_FOR_MANIFEST); commit first."
    exit 1
fi

# Check if the binary exists
if [ ! -f "$BIN_FILE" ]; then
    echo "Error: Could not find firmware binary file at $BIN_FILE"
    exit 1
fi

# Check binary file size for sanity
BIN_SIZE=$(wc -c < "$BIN_FILE" | tr -d " ")
echo "Using binary: $BIN_FILE (size: $BIN_SIZE bytes)"

# Name the binary after its version
BIN_FILENAME="firmware-${VERSION_FOR_MANIFEST}.bin"

MANIFEST_NAME="manifest.json"
if [[ "$CHANNEL" != "prod" ]]; then
  MANIFEST_NAME="manifest-${CHANNEL}.json"
fi

# Create the manifest
cat > ${MANIFEST_NAME} << EOF
{
    "version": "${VERSION_FOR_MANIFEST}",
    "build_timestamp": "${BUILD_ISO_TIME}",
    "build_timestamp_epoch": ${BUILD_TIMESTAMP},
    "git_describe": "${GIT_DESCRIBE}",
    "url": "${BASE_URL}/${BIN_FILENAME}"
}
EOF

echo "Created manifest file ${MANIFEST_NAME} with version ${VERSION_FOR_MANIFEST}, timestamp ${BUILD_ISO_TIME} (UTC)"

# Copy bin file to a new name with git hash
cp "${BIN_FILE}" "${BIN_FILENAME}"

# Upload files to server
echo "Uploading firmware and manifest to ${SERVER}:${REMOTE_DIR} (channel=${CHANNEL})..."
scp "${BIN_FILENAME}" "${MANIFEST_NAME}" "${SERVER}:${REMOTE_DIR}/"

# Clean up local temporary files
rm "${BIN_FILENAME}" "${MANIFEST_NAME}"

echo "Deployment complete. OTA update is available at ${BASE_URL}/${MANIFEST_NAME}"