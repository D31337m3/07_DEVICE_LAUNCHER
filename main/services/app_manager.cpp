#include "services/app_manager.h"

#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_log.h"
#include "esp_crc.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app_manager";

// App registry
static app_metadata_t s_apps[APP_MAX_INSTALLED];
static int s_app_count = 0;
static bool s_initialized = false;

// Running app tracking
static TaskHandle_t s_running_app_task = NULL;
static char s_running_app_name[APP_NAME_MAX_LEN] = {0};

// Storage paths
#define FLASH_APP_PATH "/storage/apps"
#define SD_APP_PATH "/sdcard/apps"

static esp_err_t load_app_header(const char *path, app_header_t *header)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s", path);
        return ESP_FAIL;
    }

    size_t read = fread(header, 1, sizeof(app_header_t), f);
    fclose(f);

    if (read != sizeof(app_header_t)) {
        ESP_LOGE(TAG, "Failed to read header from %s", path);
        return ESP_FAIL;
    }

    if (header->magic != APP_MAGIC) {
        ESP_LOGE(TAG, "Invalid magic number in %s", path);
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t load_app_icon(const char *path, app_metadata_t *meta)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return ESP_FAIL;
    }

    app_header_t header;
    fread(&header, 1, sizeof(app_header_t), f);

    if (header.icon_size == 0 || header.icon_offset == 0) {
        fclose(f);
        return ESP_ERR_NOT_FOUND;
    }

    // Allocate icon data
    meta->icon_data = malloc(header.icon_size);
    if (!meta->icon_data) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    // Read icon data
    fseek(f, header.icon_offset, SEEK_SET);
    size_t read = fread(meta->icon_data, 1, header.icon_size, f);
    fclose(f);

    if (read != header.icon_size) {
        free(meta->icon_data);
        meta->icon_data = NULL;
        return ESP_FAIL;
    }

    // Setup LVGL image descriptor
    meta->icon.header.cf = LV_IMG_CF_TRUE_COLOR;
    meta->icon.header.w = APP_ICON_WIDTH;
    meta->icon.header.h = APP_ICON_HEIGHT;
    meta->icon.data = (const uint8_t*)meta->icon_data;
    meta->icon.data_size = header.icon_size;

    return ESP_OK;
}

static app_category_t parse_category(const char *category_str)
{
    if (strcmp(category_str, "system") == 0) return APP_CATEGORY_SYSTEM;
    if (strcmp(category_str, "game") == 0) return APP_CATEGORY_GAME;
    if (strcmp(category_str, "media") == 0) return APP_CATEGORY_MEDIA;
    if (strcmp(category_str, "productivity") == 0) return APP_CATEGORY_PRODUCTIVITY;
    if (strcmp(category_str, "utility") == 0) return APP_CATEGORY_UTILITY;
    if (strcmp(category_str, "communication") == 0) return APP_CATEGORY_COMMUNICATION;
    return APP_CATEGORY_OTHER;
}

static uint32_t get_crash_count(const char *app_name)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("app_crashes", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return 0;
    }

    uint32_t count = 0;
    nvs_get_u32(nvs_handle, app_name, &count);
    nvs_close(nvs_handle);
    return count;
}

static void increment_crash_count(const char *app_name)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("app_crashes", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return;
    }

    uint32_t count = 0;
    nvs_get_u32(nvs_handle, app_name, &count);
    count++;
    nvs_set_u32(nvs_handle, app_name, count);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
}

static void reset_crash_count(const char *app_name)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("app_crashes", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return;
    }

    nvs_erase_key(nvs_handle, app_name);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
}

static esp_err_t scan_directory(const char *dir_path, app_storage_t storage)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGW(TAG, "Cannot open directory: %s", dir_path);
        return ESP_ERR_NOT_FOUND;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && s_app_count < APP_MAX_INSTALLED) {
        // Check for .app extension
        size_t len = strlen(entry->d_name);
        if (len < 5 || strcmp(entry->d_name + len - 4, ".app") != 0) {
            continue;
        }

        // Build full path
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        // Load header
        app_header_t header;
        if (load_app_header(path, &header) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to load header from %s", path);
            continue;
        }

        // Populate metadata
        app_metadata_t *meta = &s_apps[s_app_count];
        memset(meta, 0, sizeof(app_metadata_t));
        
        memcpy(meta->name, header.name, APP_NAME_MAX_LEN - 1);
        memcpy(meta->creator, header.creator, APP_CREATOR_MAX_LEN - 1);
        meta->category = parse_category(header.category);
        meta->size = header.size;
        meta->state = APP_STATE_STOPPED;
        meta->storage = storage;
        memcpy(meta->path, path, strnlen(path, sizeof(meta->path) - 1));
        meta->crash_count = get_crash_count(meta->name);

        // Load icon
        load_app_icon(path, meta);

        ESP_LOGI(TAG, "Found app: %s (%s, %lu bytes)", meta->name, meta->creator, meta->size);
        s_app_count++;
    }

    closedir(dir);
    return ESP_OK;
}

esp_err_t app_manager_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing app manager");

    memset(s_apps, 0, sizeof(s_apps));
    s_app_count = 0;

    // Scan for apps
    app_manager_scan();

    s_initialized = true;
    return ESP_OK;
}

esp_err_t app_manager_scan(void)
{
    s_app_count = 0;

    // Clean up existing icon data
    for (int i = 0; i < APP_MAX_INSTALLED; i++) {
        if (s_apps[i].icon_data) {
            free(s_apps[i].icon_data);
            s_apps[i].icon_data = NULL;
        }
    }

    // Scan flash storage
    scan_directory(FLASH_APP_PATH, APP_STORAGE_FLASH);

    // Scan SD card
    scan_directory(SD_APP_PATH, APP_STORAGE_SD);

    ESP_LOGI(TAG, "Scan complete: %d apps found", s_app_count);
    return ESP_OK;
}

int app_manager_get_app_count(void)
{
    return s_app_count;
}

const app_metadata_t* app_manager_get_app(int index)
{
    if (index < 0 || index >= s_app_count) {
        return NULL;
    }
    return &s_apps[index];
}

const app_metadata_t* app_manager_get_app_by_name(const char *name)
{
    for (int i = 0; i < s_app_count; i++) {
        if (strcmp(s_apps[i].name, name) == 0) {
            return &s_apps[i];
        }
    }
    return NULL;
}

esp_err_t app_manager_install(const char *src_path, app_storage_t dest_storage)
{
    // Load header to validate
    app_header_t header;
    if (load_app_header(src_path, &header) != ESP_OK) {
        ESP_LOGE(TAG, "Invalid app file: %s", src_path);
        return ESP_FAIL;
    }

    // Check size limit
    if (header.size > APP_MAX_SIZE) {
        ESP_LOGE(TAG, "App too large: %lu bytes (max %d)", header.size, APP_MAX_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }

    // Build destination path
    const char *dest_dir = (dest_storage == APP_STORAGE_FLASH) ? FLASH_APP_PATH : SD_APP_PATH;
    char dest_path[128];
    snprintf(dest_path, sizeof(dest_path), "%s/%s.app", dest_dir, header.name);

    // Copy file
    FILE *src = fopen(src_path, "rb");
    FILE *dst = fopen(dest_path, "wb");
    
    if (!src || !dst) {
        if (src) fclose(src);
        if (dst) fclose(dst);
        ESP_LOGE(TAG, "Failed to open files for copy");
        return ESP_FAIL;
    }

    char buffer[512];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, n, dst);
    }

    fclose(src);
    fclose(dst);

    ESP_LOGI(TAG, "Installed app: %s to %s", header.name, dest_dir);

    // Rescan
    app_manager_scan();
    return ESP_OK;
}

esp_err_t app_manager_uninstall(const char *app_name)
{
    const app_metadata_t *meta = app_manager_get_app_by_name(app_name);
    if (!meta) {
        return ESP_ERR_NOT_FOUND;
    }

    // Stop if running
    if (meta->state == APP_STATE_RUNNING) {
        app_manager_stop(app_name);
    }

    // Delete file
    if (remove(meta->path) != 0) {
        ESP_LOGE(TAG, "Failed to delete %s", meta->path);
        return ESP_FAIL;
    }

    // Reset crash count
    reset_crash_count(app_name);

    ESP_LOGI(TAG, "Uninstalled app: %s", app_name);

    // Rescan
    app_manager_scan();
    return ESP_OK;
}

esp_err_t app_manager_transfer(const char *app_name, app_storage_t dest_storage)
{
    const app_metadata_t *meta = app_manager_get_app_by_name(app_name);
    if (!meta) {
        return ESP_ERR_NOT_FOUND;
    }

    if (meta->storage == dest_storage) {
        return ESP_OK; // Already at destination
    }

    // Install to new location
    esp_err_t err = app_manager_install(meta->path, dest_storage);
    if (err != ESP_OK) {
        return err;
    }

    // Delete from old location
    remove(meta->path);

    // Rescan
    app_manager_scan();
    return ESP_OK;
}

esp_err_t app_manager_start(const char *app_name, lv_obj_t *parent_screen)
{
    // TODO: Implement app loading and execution
    // This requires dynamic loading, which is complex on ESP32
    // For now, return not implemented
    ESP_LOGW(TAG, "app_manager_start not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_manager_stop(const char *app_name)
{
    // TODO: Implement app stop
    ESP_LOGW(TAG, "app_manager_stop not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_manager_suspend(const char *app_name)
{
    ESP_LOGW(TAG, "app_manager_suspend not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t app_manager_resume(const char *app_name)
{
    ESP_LOGW(TAG, "app_manager_resume not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

bool app_manager_is_running(const char *app_name)
{
    return strcmp(s_running_app_name, app_name) == 0 && s_running_app_task != NULL;
}

app_state_t app_manager_get_state(const char *app_name)
{
    const app_metadata_t *meta = app_manager_get_app_by_name(app_name);
    if (!meta) {
        return APP_STATE_STOPPED;
    }
    return meta->state;
}
