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

# Check for uncommitted changes
if [[ -n $(git status --porcelain) ]]; then
    echo "Error: Git working directory is not clean. Please commit or stash your changes first."
    echo "Uncommitted changes:"
    git status --short
    exit 1
fi

# Get git information
GIT_HASH=$(git rev-parse --short HEAD)
if [ -z "$GIT_HASH" ]; then
    echo "Error: Could not get git hash. Make sure you're in a git repository."
    exit 1
fi

# Get the full git describe output and format it uniformly
RAW_GIT_DESCRIBE=$(git describe --tags --always --dirty 2>/dev/null || echo "v0.0-g$GIT_HASH")

# Check if Git describe contains "dirty"
if [[ "$RAW_GIT_DESCRIBE" == *-dirty ]]; then
    echo "Error: Git working directory contains uncommitted changes. Please commit all changes first."
    exit 1
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

# Build firmware first to embed BUILD_TIMESTAMP used by both firmware and web manifest
echo "Building project (firmware) with idf.py..."
idf.py fullclean build || { echo "Error: Build failed"; exit 1; }
echo "Firmware build successful"

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

# Read the build timestamp that was written by CMake during build
TIMESTAMP_FILE="build/firmware_build_timestamp.txt"

if [ ! -f "$TIMESTAMP_FILE" ]; then
    echo "Error: Build timestamp file not found at $TIMESTAMP_FILE"
    echo "Make sure to run 'idf.py build' first"
    exit 1
fi

BUILD_TIMESTAMP=$(cat "$TIMESTAMP_FILE")

if [ -z "$BUILD_TIMESTAMP" ]; then
    echo "Error: Build timestamp file is empty"
    exit 1
fi

echo "Using firmware build timestamp: $BUILD_TIMESTAMP"

# Generate ISO format time from the timestamp
BUILD_ISO_TIME=$(date -u -r $BUILD_TIMESTAMP +"%Y-%m-%dT%H:%M:%SZ")
echo "Using embedded build timestamp (UTC): $BUILD_ISO_TIME ($BUILD_TIMESTAMP)"

# Check if the binary exists
if [ ! -f "$BIN_FILE" ]; then
    echo "Error: Could not find firmware binary file at $BIN_FILE"
    exit 1
fi

# Check binary file size for sanity
BIN_SIZE=$(stat -f%z "$BIN_FILE")
echo "Using binary: $BIN_FILE (size: $BIN_SIZE bytes)"

# Create filenames with git hash
BIN_FILENAME="firmware-${GIT_HASH}.bin"
WEB_FILENAME="index-${GIT_HASH}.html.gz"
MANIFEST_FILE="manifest.json"

# Produce gzipped web asset in repo root for upload
gzip -c -9 "$INDEX_HTML" > "$WEB_FILENAME"

# Create manifest.json with firmware and web information
cat > ${MANIFEST_FILE} << EOF
{
    "version": "${VERSION_FOR_MANIFEST}",
    "build_timestamp": "${BUILD_ISO_TIME}",
    "build_timestamp_epoch": ${BUILD_TIMESTAMP},
    "git_describe": "${GIT_DESCRIBE}",
    "url": "${BASE_URL}/${BIN_FILENAME}",
    "web_version": "${VERSION_FOR_MANIFEST}",
    "web_build_timestamp": "${BUILD_ISO_TIME}",
    "web_build_timestamp_epoch": ${BUILD_TIMESTAMP},
    "web_git_describe": "${GIT_DESCRIBE}",
    "web_url": "${BASE_URL}/${WEB_FILENAME}"
}
EOF

echo "Created manifest file with version ${VERSION_FOR_MANIFEST}, timestamp ${BUILD_ISO_TIME} (UTC)"

# Copy bin file to a new name with git hash
cp "${BIN_FILE}" "${BIN_FILENAME}"

# Upload files to server
echo "Uploading firmware, web asset, and manifest to ${SERVER}:${REMOTE_DIR}..."
scp "${BIN_FILENAME}" "${WEB_FILENAME}" "${MANIFEST_FILE}" "${SERVER}:${REMOTE_DIR}/"

# Clean up local temporary files
rm "${BIN_FILENAME}" "${WEB_FILENAME}" "${MANIFEST_FILE}"

echo "Deployment complete. OTA update is available at ${BASE_URL}/${MANIFEST_FILE}"