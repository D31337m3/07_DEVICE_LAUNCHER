#include "services/storage_service.h"

#include <sys/stat.h>
#include <string.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"

static const char *TAG = "storage";

static const char *kMountPoint = "/storage";
static bool s_mounted = false;
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;
static esp_err_t s_last_err = ESP_OK;

static void ensure_dir(const char *path)
{
    if (!path || path[0] == '\0') {
        return;
    }
    struct stat st;
    if (stat(path, &st) == 0) {
        return;
    }
    (void)mkdir(path, 0775);
}

esp_err_t storage_service_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    s_last_err = ESP_FAIL;

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(kMountPoint, "storage", &mount_config, &s_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Mount failed: %s", esp_err_to_name(err));
        s_last_err = err;
        s_mounted = false;
        s_wl_handle = WL_INVALID_HANDLE;
        return err;
    }

    s_mounted = true;
    s_last_err = ESP_OK;
    ESP_LOGI(TAG, "Mounted at %s", kMountPoint);

    // Ensure expected directories exist.
    ensure_dir("/storage/apps");
    ensure_dir("/storage/files");

    return ESP_OK;
}

bool storage_service_is_mounted(void)
{
    return s_mounted;
}

const char *storage_service_mount_point(void)
{
    return kMountPoint;
}

esp_err_t storage_service_last_error(void)
{
    return s_last_err;
}
