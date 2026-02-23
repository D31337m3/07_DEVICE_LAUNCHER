#pragma once

#include "esp_err.h"

// Registers an LVGL filesystem driver:
// - Drive letter: 'S'
// - LVGL paths like "S:/foo.jpg" map to VFS paths like "/sdcard/foo.jpg".
// Safe to call multiple times.
esp_err_t lvgl_fs_sdcard_init(void);
