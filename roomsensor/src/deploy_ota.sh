#!/bin/bash
set -e

# Configuration
SERVER="gaia"
REMOTE_DIR="/mnt/containers/data/updates"
BASE_URL="https://updates.gaia.bio"
BIN_FILE="build/sensorv2.bin"

# JS build paths
ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
JS_DIR="$ROOT_DIR/js"
JS_BUILD_DIR="$JS_DIR/dist"

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

# Get git information
GIT_HASH=$(git rev-parse --short HEAD)
if [ -z "$GIT_HASH" ]; then
    echo "Error: Could not get git hash. Make sure you're in a git repository."
    exit 1
fi

# Get the full git describe output and format it uniformly
RAW_GIT_DESCRIBE=$(git describe --tags --always --dirty 2>/dev/null || echo "v0.0-g$GIT_HASH")

# For prod, enforce clean; for dev, allow dirty
if [[ "$CHANNEL" == "prod" ]]; then
  if [[ "$RAW_GIT_DESCRIBE" == *-dirty ]]; then
      echo "Error: Git working directory contains uncommitted changes. Please commit all changes first."
      exit 1
  fi
fi

# Format versions consistently: git hash for clean, revYYYYMMDDHHMMSS-shortHash-dirty for dirty
TIMESTAMP=$(date -u +"%Y%m%d%H%M%S")
if [[ "$RAW_GIT_DESCRIBE" == *-dirty ]]; then
    # For dirty builds, use uniform format
    GIT_DESCRIBE="rev${TIMESTAMP}-${GIT_HASH}-dirty"
    VERSION_FOR_MANIFEST="$GIT_DESCRIBE"
else
    # For clean builds, use just the git hash
    GIT_DESCRIBE="$RAW_GIT_DESCRIBE"  # Keep original for logging
    VERSION_FOR_MANIFEST="$GIT_HASH"  # Use hash for manifest
fi
echo "Git describe: $GIT_DESCRIBE"
echo "Version for manifest: $VERSION_FOR_MANIFEST"

# Choose artifact suffix: use full rev timestamp form for dirty builds, short hash for clean
if [[ "$RAW_GIT_DESCRIBE" == *-dirty ]]; then
    ARTIFACT_SUFFIX="$VERSION_FOR_MANIFEST"
else
    ARTIFACT_SUFFIX="$GIT_HASH"
fi

# Build firmware first, which runs update_version.sh to generate version JSON files
echo "Building project (firmware) with idf.py..."
idf.py build || { echo "Error: Build failed"; exit 1; }
echo "Firmware build successful"

# Now that the build is done, read the generated version info from the JSON files
if [ ! -f "fsdata/firmware.json" ] || [ ! -f "fsdata/webapp.json" ]; then
    echo "Error: Version JSON files not found in fsdata/ after build."
    exit 1
fi

# Use jq to extract values to ensure robustness
VERSION_FOR_MANIFEST=$(jq -r '.local_version' fsdata/firmware.json)
BUILD_TIMESTAMP=$(jq -r '.local_build_timestamp_epoch' fsdata/firmware.json)
BUILD_ISO_TIME=$(jq -r '.local_build_timestamp' fsdata/firmware.json)
GIT_DESCRIBE=$(jq -r '.local_git_describe' fsdata/firmware.json)

WEB_VERSION=$(jq -r '.local_version' fsdata/webapp.json)
WEB_BUILD_TIMESTAMP=$(jq -r '.local_build_timestamp_epoch' fsdata/webapp.json)
WEB_BUILD_ISO_TIME=$(jq -r '.local_build_timestamp' fsdata/webapp.json)
WEB_GIT_DESCRIBE=$(jq -r '.local_git_describe' fsdata/webapp.json)

echo "Using firmware version: $VERSION_FOR_MANIFEST ($BUILD_ISO_TIME)"
echo "Using webapp version: $WEB_VERSION ($WEB_BUILD_ISO_TIME)"

# Build web app (Vite) and gzip index.html
echo "Building web app..."
pushd "$JS_DIR" >/dev/null
npm ci
npm run build || { echo "Error: Web build failed"; popd >/dev/null; exit 1; }
popd >/dev/null

INDEX_HTML="$JS_BUILD_DIR/index.html"
if [ ! -f "$INDEX_HTML" ]; then
    echo "Error: Built index.html not found at $INDEX_HTML"
    exit 1
fi

# Check if the binary exists
if [ ! -f "$BIN_FILE" ]; then
    echo "Error: Could not find firmware binary file at $BIN_FILE"
    exit 1
fi

# Check binary file size for sanity
BIN_SIZE=$(stat -f%z "$BIN_FILE")
echo "Using binary: $BIN_FILE (size: $BIN_SIZE bytes)"

# Create filenames with git hash
BIN_FILENAME="firmware-${ARTIFACT_SUFFIX}.bin"
WEB_FILENAME="index-${ARTIFACT_SUFFIX}.html.gz"
MANIFEST_FILE="manifest.json"

# Produce gzipped web asset in repo root for upload
gzip -c -9 "$INDEX_HTML" > "$WEB_FILENAME"

MANIFEST_NAME="manifest.json"
if [[ "$CHANNEL" != "prod" ]]; then
  MANIFEST_NAME="manifest-${CHANNEL}.json"
fi

# Create manifest with firmware and web information
cat > ${MANIFEST_NAME} << EOF
{
    "version": "${VERSION_FOR_MANIFEST}",
    "build_timestamp": "${BUILD_ISO_TIME}",
    "build_timestamp_epoch": ${BUILD_TIMESTAMP},
    "git_describe": "${GIT_DESCRIBE}",
    "url": "${BASE_URL}/${BIN_FILENAME}",
    "web_version": "${WEB_VERSION}",
    "web_build_timestamp": "${WEB_BUILD_ISO_TIME}",
    "web_build_timestamp_epoch": ${WEB_BUILD_TIMESTAMP},
    "web_git_describe": "${WEB_GIT_DESCRIBE}",
    "web_url": "${BASE_URL}/${WEB_FILENAME}"
}
EOF

echo "Created manifest file ${MANIFEST_NAME} with version ${VERSION_FOR_MANIFEST}, timestamp ${BUILD_ISO_TIME} (UTC)"

# The firmware.json is already correctly in fsdata/ from the build step, no need to overwrite

# Copy bin file to a new name with git hash
cp "${BIN_FILE}" "${BIN_FILENAME}"

# Upload files to server
echo "Uploading firmware, web asset, and manifest to ${SERVER}:${REMOTE_DIR} (channel=${CHANNEL})..."
scp "${BIN_FILENAME}" "${WEB_FILENAME}" "${MANIFEST_NAME}" "${SERVER}:${REMOTE_DIR}/"

# Clean up local temporary files
rm "${BIN_FILENAME}" "${WEB_FILENAME}" "${MANIFEST_NAME}"

echo "Deployment complete. OTA update is available at ${BASE_URL}/${MANIFEST_NAME}"