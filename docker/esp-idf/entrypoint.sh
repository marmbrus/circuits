#!/usr/bin/env bash
# Quiet replacement for the espressif/idf entrypoint: load the IDF environment, then run the command.
set -e
. "$IDF_PATH/export.sh" > /dev/null
exec "$@"
