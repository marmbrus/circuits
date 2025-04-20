#!/bin/bash
set -e

# Configuration
SERVER="gaia"
REMOTE_DIR="/home/marmbrus/gaia/webapp/public"
BASE_URL="http://gaia.home:3000"
BIN_FILE="build/surprise.bin"

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

# Get the full git describe output which includes tag information
GIT_DESCRIBE=$(git describe --tags --always --dirty 2>/dev/null || echo "v0.0-g$GIT_HASH")
echo "Git describe: $GIT_DESCRIBE"

# Check if Git describe contains "dirty"
if [[ "$GIT_DESCRIBE" == *-dirty ]]; then
    echo "Error: Git working directory contains uncommitted changes. Please commit all changes first."
    exit 1
fi

# Run idf.py build first to ensure the binary is up-to-date
echo "Building project with idf.py..."
idf.py fullclean build || { echo "Error: Build failed"; exit 1; }
echo "Build successful"

# Extract build timestamp from CMake cache instead of generating a new one
BUILD_TIMESTAMP=$(grep -a "BUILD_TIMESTAMP:STRING=" build/CMakeCache.txt | cut -d= -f2)
if [ -z "$BUILD_TIMESTAMP" ]; then
    echo "Error: Could not extract BUILD_TIMESTAMP from CMake cache"
    exit 1
fi

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

# Create bin filename with git hash
BIN_FILENAME="firmware-${GIT_HASH}.bin"
MANIFEST_FILE="manifest.json"

# Create manifest.json with version information
cat > ${MANIFEST_FILE} << EOF
{
    "version": "${GIT_HASH}",
    "build_timestamp": "${BUILD_ISO_TIME}",
    "build_timestamp_epoch": ${BUILD_TIMESTAMP},
    "git_describe": "${GIT_DESCRIBE}",
    "url": "${BASE_URL}/${BIN_FILENAME}"
}
EOF

echo "Created manifest file with version ${GIT_HASH}, timestamp ${BUILD_ISO_TIME} (UTC)"

# Copy bin file to a new name with git hash
cp "${BIN_FILE}" "${BIN_FILENAME}"

# Upload files to server
echo "Uploading firmware binary and manifest to ${SERVER}:${REMOTE_DIR}..."
scp "${BIN_FILENAME}" "${MANIFEST_FILE}" "${SERVER}:${REMOTE_DIR}/"

# Clean up local temporary files
rm "${BIN_FILENAME}" "${MANIFEST_FILE}"

echo "Deployment complete. OTA update is available at ${BASE_URL}/${MANIFEST_FILE}"