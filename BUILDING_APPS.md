# ESP32-S3 Device Launcher - App Development Guide

## Overview
This device launcher provides a carousel-style app management system for ESP32-S3 Touch AMOLED displays. Apps can be built-in (compiled into firmware) or loaded from the filesystem (.app files).

## Quick Start - Built-in Apps

### 1. Create Your App UI Files

**myapp.h** (in `main/include/`):
```cpp
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_myapp_init(void);

#ifdef __cplusplus
}
#endif
```

**myapp.cpp** (in `main/`):
```cpp
#include "myapp.h"
#include "lvgl.h"
#include "esp_log.h"
#include "app_pins.h"
#include "ui_app_carousel.h"  // For returning to carousel

static const char *TAG = "myapp";
static lv_obj_t *s_myapp_screen = nullptr;

esp_err_t ui_myapp_init(void)
{
    ESP_LOGI(TAG, "Initializing MyApp");
    
    // Create fullscreen app
    s_myapp_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_myapp_screen, lv_color_hex(0x000000), 0);
    
    // Header with Exit button
    lv_obj_t *header = lv_obj_create(s_myapp_screen);
    lv_obj_set_size(header, APP_LCD_H_RES, 44);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_width(header, 0, 0);
    
    // Exit button (returns to carousel)
    lv_obj_t *btn_exit = lv_btn_create(header);
    lv_obj_set_size(btn_exit, 60, 32);
    lv_obj_align(btn_exit, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_t *lbl_exit = lv_label_create(btn_exit);
    lv_label_set_text(lbl_exit, "Exit");
    lv_obj_center(lbl_exit);
    lv_obj_add_event_cb(btn_exit, [](lv_event_t *) {
        ui_app_carousel_init();  // Return to carousel
    }, LV_EVENT_CLICKED, NULL);
    
    // Title
    lv_obj_t *lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "MyApp");
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);
    
    // Your app content here (below header)
    lv_obj_t *content = lv_obj_create(s_myapp_screen);
    lv_obj_set_size(content, APP_LCD_H_RES, APP_LCD_V_RES - 50);
    lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x0a0a0a), 0);
    
    lv_obj_t *label = lv_label_create(content);
    lv_label_set_text(label, "Hello from MyApp!");
    lv_obj_center(label);
    
    // Load the screen
    lv_scr_load(s_myapp_screen);
    
    ESP_LOGI(TAG, "MyApp initialized");
    return ESP_OK;
}
```

### 2. Register App in Carousel

Edit `main/ui_app_carousel.cpp`:

```cpp
// At top with other static vars:
static app_metadata_t s_builtin_myapp;

// In register_builtin_apps():
memset(&s_builtin_myapp, 0, sizeof(app_metadata_t));
strncpy(s_builtin_myapp.name, "MyApp", APP_NAME_MAX_LEN - 1);
strncpy(s_builtin_myapp.creator, "Your Name", APP_CREATOR_MAX_LEN - 1);
s_builtin_myapp.category = APP_CATEGORY_UTILITY;  // or GAME, MEDIA, etc.
s_builtin_myapp.size = 0;
s_builtin_myapp.state = APP_STATE_STOPPED;
s_builtin_myapp.storage = APP_STORAGE_FLASH;
strncpy(s_builtin_myapp.path, "<builtin>", sizeof(s_builtin_myapp.path) - 1);
s_builtin_myapp.crash_count = 0;
s_builtin_myapp.icon_data = nullptr;

ESP_LOGI(TAG, "Registered built-in apps: Settings, Terminal, MyApp");

// In register_builtin_apps() - update total count:
s_total_apps = 3 + app_manager_get_app_count();  // Changed from 2 to 3

// In get_app_at_index():
if (index == 0) return &s_builtin_settings;
if (index == 1) return &s_builtin_terminal;
if (index == 2) return &s_builtin_myapp;  // ADD THIS

int app_manager_index = index - 3;  // Changed from index - 2

// In launch_current_app():
if (s_current_app_index == 0) {
    ui_launcher_init();
} else if (s_current_app_index == 1) {
    ui_terminal_init();
} else if (s_current_app_index == 2) {
    ui_myapp_init();  // ADD THIS
}
```

### 3. Add to Build System

Edit `main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS
        "main.cpp"
        "display_lvgl.cpp"
        "i2c_bus.cpp"
        "ui_launcher.cpp"
        "ui_app_carousel.cpp"
        "ui_terminal.cpp"
        "myapp.cpp"  # ADD YOUR FILE
        ...
```

### 4. Include Header

Edit `main/ui_app_carousel.cpp` includes:

```cpp
#include "ui_app_carousel.h"
#include "display_lvgl.h"
#include "services/app_manager.h"
#include "ui_launcher.h"
#include "ui_terminal.h"
#include "myapp.h"  // ADD THIS
```

### 5. Build and Flash

```powershell
cd F:\ESP32-S3-Touch-AMOLED-1.8-Demo\ESP-IDF-v5.3.2\07_DEVICE_LAUNCHER

# Set up environment
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\python_env\idf5.5_py3.13_env"
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5.1"

# Clean build
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue

# Build
idf.py build

# Flash
python -m esptool --chip esp32s3 --port COM6 --baud 460800 write_flash 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0xf000 build/ota_data_initial.bin 0x20000 build/device_launcher.bin
```

## Hardware Specs

**Display:**
- Resolution: 368x448 (portrait)
- Type: SH8601 QSPI AMOLED
- Touch: FT5x06 capacitive

**Memory:**
- Flash: 16MB
- PSRAM: 8MB Octal

**Key Pins:** (see `main/include/app_pins.h`)
- I2C: SDA=GPIO15, SCL=GPIO14 (shared: Touch, PMU, Audio, IMU)
- Display: CS=GPIO6, SCK=GPIO47, MOSI=GPIO18
- Audio: ES8311 codec on I2C
- Power: AXP2101 PMU on I2C

## LVGL Tips

**Available Fonts:**
- `lv_font_montserrat_12`
- `lv_font_montserrat_14`
- `lv_font_montserrat_16`
- `lv_font_montserrat_20`
- `lv_font_montserrat_24`

**Screen Dimensions:**
```cpp
APP_LCD_H_RES = 368  // Width (portrait)
APP_LCD_V_RES = 448  // Height (portrait)
```

**Display Locking (CRITICAL):**
```cpp
if (display_lvgl_lock(-1)) {
    // Modify LVGL objects here
    display_lvgl_unlock();
}
```

**Common Patterns:**
```cpp
// Full screen container
lv_obj_t *screen = lv_obj_create(NULL);
lv_scr_load(screen);

// Button with click handler
lv_obj_t *btn = lv_btn_create(parent);
lv_obj_add_event_cb(btn, [](lv_event_t *e) {
    ESP_LOGI("TAG", "Button clicked!");
}, LV_EVENT_CLICKED, NULL);

// Color from hex
lv_obj_set_style_bg_color(obj, lv_color_hex(0xFF0000), 0);
```

## Services Available

All services in `main/services/`:
- **power_axp2101**: Battery, charging, power management
- **audio_es8311**: Audio playback/recording
- **wifi_service**: WiFi connection management
- **ble_service**: Bluetooth LE 
- **time_service**: RTC and time sync
- **imu_qmi8658**: Accelerometer/gyroscope
- **sdcard_service**: SD card read/write
- **ota_service**: Over-the-air updates
- **app_manager**: App installation/management

## Example: Using Services

```cpp
#include "services/audio_es8311.h"

// Play spray can sound effect
audio_es8311_play_spray_rattle();

// Or play tone
audio_es8311_play_tone(440, 1000);  // 440Hz for 1 second
```

## Debugging

**Serial Monitor:**
```powershell
idf.py monitor
```

**Log Levels:**
```cpp
ESP_LOGI(TAG, "Info message");
ESP_LOGW(TAG, "Warning");
ESP_LOGE(TAG, "Error");
ESP_LOGD(TAG, "Debug (only if enabled)");
```

## Common Issues

**Build Fails - Font Not Found:**
- Only use fonts listed above (12, 14, 16, 20, 24)
- Don't use lv_font_montserrat_32, _40, _48 (not compiled)

**I2C Conflicts:**
- I2C bus is shared - only initialize ONCE in main.cpp
- Don't call i2c_bus_init() from apps

**Display Buffer Crash:**
- Always use MALLOC_CAP_SPIRAM for large allocations
- Never use MALLOC_CAP_DMA for display buffers

**App Not Showing in Carousel:**
- Check you incremented `s_total_apps` count
- Verify get_app_at_index() returns your app
- Ensure index math is correct (index - N where N = number of built-in apps)

## Future: Loadable .app Files

The system supports loading apps from filesystem as .app binary files. See `tools/app_builder.py` for creating .app files. This allows apps to be installed/uninstalled without reflashing firmware.

**App Binary Format:**
- 128-byte header (magic, name, creator, category, size, version)
- 32KB icon data (128x128 RGB565)
- Executable code

Currently filesystem apps are scanned but execution is not fully implemented.

## Best Practices

1. **Always provide Exit button** - users need to return to carousel
2. **Keep header consistent** - 44px tall, dark gray background
3. **Check return codes** - especially for I2C, display operations
4. **Lock display** - use display_lvgl_lock() when modifying UI
5. **Clean up resources** - delete screens/objects when exiting
6. **Test on hardware** - emulator behavior may differ
7. **Use static allocation** - heap fragmentation can cause crashes

## Resources

- LVGL Docs: https://docs.lvgl.io/
- ESP-IDF API: https://docs.espressif.com/projects/esp-idf/
- Project README: `../README.md`
- Example Apps: `ui_terminal.cpp`, `ui_launcher.cpp`

---

**Happy coding! Test your app thoroughly before deployment.**
