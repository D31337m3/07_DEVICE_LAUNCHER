#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// App binary format constants
#define APP_MAGIC 0x41505032  // "APP2" - App format version 2
#define APP_NAME_MAX_LEN 32
#define APP_CREATOR_MAX_LEN 32
#define APP_CATEGORY_MAX_LEN 16
#define APP_MAX_SIZE (500 * 1024)  // 500KB max per app
#define APP_ICON_WIDTH 128
#define APP_ICON_HEIGHT 128
#define APP_MAX_INSTALLED 16

// App categories
typedef enum {
    APP_CATEGORY_SYSTEM,
    APP_CATEGORY_GAME,
    APP_CATEGORY_MEDIA,
    APP_CATEGORY_PRODUCTIVITY,
    APP_CATEGORY_UTILITY,
    APP_CATEGORY_COMMUNICATION,
    APP_CATEGORY_OTHER
} app_category_t;

// App state
typedef enum {
    APP_STATE_STOPPED,
    APP_STATE_RUNNING,
    APP_STATE_SUSPENDED,
    APP_STATE_CRASHED
} app_state_t;

// App storage location
typedef enum {
    APP_STORAGE_FLASH,
    APP_STORAGE_SD
} app_storage_t;

// App header structure (stored at beginning of .app file)
typedef struct {
    uint32_t magic;                         // Magic number for validation
    uint32_t version;                       // App format version
    char name[APP_NAME_MAX_LEN];            // App name
    char creator[APP_CREATOR_MAX_LEN];      // Creator name
    char category[APP_CATEGORY_MAX_LEN];    // Category string
    uint32_t size;                          // Total app size in bytes
    uint32_t code_offset;                   // Offset to executable code
    uint32_t code_size;                     // Size of executable code
    uint32_t icon_offset;                   // Offset to icon data (RGB565)
    uint32_t icon_size;                     // Size of icon data
    uint32_t checksum;                      // CRC32 checksum of entire file
    uint32_t reserved[8];                   // Reserved for future use
} __attribute__((packed)) app_header_t;

// App entry point signature
typedef void (*app_entry_fn_t)(lv_obj_t *parent_screen);

// App metadata (runtime info)
typedef struct {
    char name[APP_NAME_MAX_LEN];
    char creator[APP_CREATOR_MAX_LEN];
    app_category_t category;
    uint32_t size;
    app_state_t state;
    app_storage_t storage;
    char path[64];                          // File path
    uint32_t crash_count;
    lv_img_dsc_t icon;                      // LVGL image descriptor for icon
    void *icon_data;                        // Icon pixel data
} app_metadata_t;

// App manager API
esp_err_t app_manager_init(void);
esp_err_t app_manager_scan(void);
int app_manager_get_app_count(void);
const app_metadata_t* app_manager_get_app(int index);
const app_metadata_t* app_manager_get_app_by_name(const char *name);

esp_err_t app_manager_install(const char *src_path, app_storage_t dest_storage);
esp_err_t app_manager_uninstall(const char *app_name);
esp_err_t app_manager_transfer(const char *app_name, app_storage_t dest_storage);

esp_err_t app_manager_start(const char *app_name, lv_obj_t *parent_screen);
esp_err_t app_manager_stop(const char *app_name);
esp_err_t app_manager_suspend(const char *app_name);
esp_err_t app_manager_resume(const char *app_name);

bool app_manager_is_running(const char *app_name);
app_state_t app_manager_get_state(const char *app_name);

#ifdef __cplusplus
}
#endif
