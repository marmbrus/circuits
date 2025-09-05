#include "filesystem.h"

#include "esp_log.h"
#include "esp_littlefs.h"
#include <sys/stat.h>
#include <cstdio>

static const char* TAG_FS = "webfs";

namespace webfs {

static bool s_mounted = false;
static char s_base_path[16] = "/storage"; // default base path

esp_err_t init(const char* partition_label, bool format_if_mount_failed)
{
    if (s_mounted) {
        return ESP_OK;
    }

    esp_vfs_littlefs_conf_t conf = {
        .base_path = s_base_path,
        .partition_label = partition_label,
        .format_if_mount_failed = false,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG_FS, "Failed to mount or format LittleFS (label: %s)", partition_label);
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG_FS, "LittleFS partition not found (label: %s)", partition_label);
        } else {
            ESP_LOGE(TAG_FS, "LittleFS register failed (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(partition_label, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG_FS, "Mounted LittleFS '%s' total=%u bytes used=%u bytes", partition_label, (unsigned)total, (unsigned)used);
    } else {
        ESP_LOGW(TAG_FS, "esp_littlefs_info failed: %s", esp_err_to_name(ret));
    }

    s_mounted = true;
    return ESP_OK;
}

bool exists(const char* absolute_path)
{
    struct stat st;
    return stat(absolute_path, &st) == 0 && S_ISREG(st.st_mode);
}

esp_err_t read_file(const char* absolute_path, std::vector<uint8_t>& out_contents)
{
    FILE* f = fopen(absolute_path, "rb");
    if (!f) {
        return ESP_FAIL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return ESP_FAIL;
    }
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return ESP_FAIL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return ESP_FAIL;
    }
    out_contents.resize(static_cast<size_t>(size));
    size_t read_n = fread(out_contents.data(), 1, static_cast<size_t>(size), f);
    fclose(f);
    return (read_n == static_cast<size_t>(size)) ? ESP_OK : ESP_FAIL;
}

} // namespace webfs


