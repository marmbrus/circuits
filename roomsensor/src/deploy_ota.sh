#!/bin/bash
set -e

# Configuration
SERVER="gaia"
REMOTE_DIR="/home/marmbrus/gaia/webapp/public"
BASE_URL="http://gaia.home:3000"
BIN_FILE="build/surprise.bin"

# Check if the binary exists
if [ ! -f "$BIN_FILE" ]; then
    echo "Error: Could not find firmware binary file at $BIN_FILE"
    exit 1
fi

# Check binary file size for sanity
BIN_SIZE=$(stat -f%z "$BIN_FILE")
echo "Using binary: $BIN_FILE (size: $BIN_SIZE bytes)"

# Get git hash
GIT_HASH=$(git rev-parse --short HEAD)
if [ -z "$GIT_HASH" ]; then
    echo "Error: Could not get git hash. Make sure you're in a git repository."
    exit 1
fi

# Create bin filename with git hash
BIN_FILENAME="firmware-${GIT_HASH}.bin"
MANIFEST_FILE="manifest.json"

# Create manifest.json
cat > ${MANIFEST_FILE} << EOF
{
    "version": "${GIT_HASH}",
    "build_timestamp": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")",
    "url": "${BASE_URL}/${BIN_FILENAME}"
}
EOF

echo "Created manifest file with version ${GIT_HASH}"

# Copy bin file to a new name with git hash
cp "${BIN_FILE}" "${BIN_FILENAME}"

# Upload files to server
echo "Uploading firmware binary and manifest to ${SERVER}:${REMOTE_DIR}..."
scp "${BIN_FILENAME}" "${MANIFEST_FILE}" "${SERVER}:${REMOTE_DIR}/"

# Clean up local temporary files
rm "${BIN_FILENAME}" "${MANIFEST_FILE}"

echo "Deployment complete. OTA update is available at ${BASE_URL}/${MANIFEST_FILE}"