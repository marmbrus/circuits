#pragma once

#include "esp_err.h"
#include <vector>
#include <cstddef>

namespace webfs {

/**
 * Initialize and mount the LittleFS partition for serving web assets.
 *
 * - Uses the VFS layer; once mounted, files can be accessed with stdio APIs.
 * - Partition label should match the entry in partitions.csv (default: "webfs").
 *
 * @param partition_label Label of the LittleFS data partition to mount.
 * @param format_if_mount_failed If true, will attempt to format the partition on mount failure.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t init(const char* partition_label = "webfs", bool format_if_mount_failed = false);

/**
 * Check if a file exists on the mounted filesystem.
 * Path must start with '/'. Example: "/index.html" or "/index.html.gz".
 */
bool exists(const char* absolute_path);

/**
 * Read entire file contents into memory.
 *
 * @param absolute_path Absolute path starting with '/'.
 * @param out_contents Output buffer to receive file bytes.
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t read_file(const char* absolute_path, std::vector<uint8_t>& out_contents);

} // namespace webfs


