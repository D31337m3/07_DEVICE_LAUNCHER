# ESP32-S3 Touch AMOLED 1.8" — Device Launcher

A full-featured device launcher firmware for the **ESP32-S3 Touch AMOLED 1.8"** module. Provides a swipeable app carousel UI, built-in system apps, a plugin `.app` system for SD card / flash-loaded apps, and comprehensive hardware service support.

> **ESP-IDF version**: v6.x (migrated from v5.x — uses new `i2c_master` API)

---

## Table of Contents

- [Hardware](#hardware)
- [Pin Configuration](#pin-configuration)
- [Partition Table](#partition-table)
- [Project Structure](#project-structure)
- [Architecture Overview](#architecture-overview)
- [Services](#services)
- [App System](#app-system)
  - [Built-in Apps](#built-in-apps)
  - [Adding a New Built-in App](#adding-a-new-built-in-app)
  - [Loadable .app Files](#loadable-app-files)
- [Building & Flashing](#building--flashing)
- [Configuration](#configuration)
- [LVGL Notes](#lvgl-notes)
- [Troubleshooting](#troubleshooting)
- [Roadmap](#roadmap)
- [License](#license)

---

## Hardware

| Component | Part | Interface |
|-----------|------|-----------|
| MCU | ESP32-S3 (dual-core Xtensa @ 240 MHz) | — |
| Flash | 16 MB | — |
| PSRAM | 8 MB Octal SPI @ 80 MHz | — |
| Display | SH8601 AMOLED 368×448 | QSPI |
| Touch | FT5x06 capacitive touch controller | I2C |
| PMU | AXP2101 power management | I2C |
| IO Expander | TCA9554 (display power sequencing) | I2C |
| IMU | QMI8658 6-axis accel/gyro | I2C |
| Audio Codec | ES8311 | I2C + I2S |
| SD Card | MicroSD | SDMMC 1-bit |
| Connectivity | WiFi 802.11 b/g/n, Bluetooth 5.0 BLE | — |

---

## Pin Configuration

### Display — SH8601 QSPI
| GPIO | Signal |
|------|--------|
| 4 | DATA0 |
| 5 | DATA1 |
| 6 | DATA2 |
| 7 | DATA3 |
| 11 | CLK |
| 12 | CS |

### Shared I2C Bus (Touch · PMU · TCA9554 · IMU · Audio)
| GPIO | Signal |
|------|--------|
| 14 | SCL (200 kHz) |
| 15 | SDA |

### Audio — ES8311 (I2S)
| GPIO | Signal |
|------|--------|
| 8 | I2S_BCK |
| 9 | I2S_WS |
| 10 | I2S_DO (DAC out) |
| 16 | I2S_MCLK |
| 45 | I2S_WS (alt) |
| 46 | PA_EN (amplifier enable) |

### SD Card — SDMMC 1-bit
| GPIO | Signal |
|------|--------|
| 1 | CMD |
| 2 | CLK |
| 3 | DATA0 |

### I2C Device Addresses
| Device | Address |
|--------|---------|
| FT5x06 touch | 0x38 |
| AXP2101 PMU | 0x34 |
| TCA9554 IO expander | 0x20 |
| QMI8658 IMU | 0x6B |
| ES8311 audio | 0x18 |
| PCF85063 RTC | 0x51 |

---

## Partition Table

16 MB flash layout (`partitions.csv`):

| Name | Type | Size | Purpose |
|------|------|------|---------|
| `nvs` | data/nvs | 24 KB | Persistent settings (brightness, wifi creds, …) |
| `otadata` | data/ota | 8 KB | OTA slot tracking |
| `phy_init` | data/phy | 4 KB | RF calibration |
| `ota_0` | app/ota_0 | 5 MB | Active firmware slot |
| `ota_1` | app/ota_1 | 5 MB | OTA update target slot |
| `storage` | data/fat | 5 MB | Internal FAT filesystem (`/storage`) |

Current firmware binary: **~1.7 MB** — plenty of headroom in 5 MB OTA partitions.

---

## Project Structure

```
07_DEVICE_LAUNCHER/
├── main/
│   ├── main.cpp                     # Entry point — boot sequence, WiFi manager, radio init, carousel
│   ├── display_lvgl.cpp/.h          # SH8601 + FT5x06 init, LVGL tick/flush task, brightness NVS
│   ├── i2c_bus.cpp                  # Single shared I2C master bus (i2c_new_master_bus)
│   ├── ui_launcher.cpp/.h           # Settings & launcher UI screens
│   ├── ui_app_carousel.cpp/.h       # Swipeable app carousel (built-in + .app files)
│   ├── ui_terminal.cpp              # Built-in terminal/shell screen
│   ├── ui_media.cpp                 # Media viewer (JPEG, GIF, MP3)
│   ├── ui_mp3.cpp                   # MP3 player UI
│   ├── ui_radio.cpp/.h              # Internet radio player UI
│   ├── ui_fileserver.cpp            # File server UI (browse /storage & /sdcard)
│   ├── wifi_manager.cpp/.h          # WiFi web-UI manager
│   ├── radio_player.cpp/.h          # Streaming radio engine
│   ├── lvgl_fs_sdcard.cpp           # LVGL filesystem driver → SD card
│   ├── include/
│   │   ├── app_pins.h               # All GPIO pin definitions
│   │   └── services/                # Service header files (one per service)
│   ├── services/
│   │   ├── boot_service.cpp         # Boot sequence: NVS → I2C → display → audio → splash
│   │   ├── power_axp2101.cpp        # AXP2101 PMU — battery %, charging, VBUS
│   │   ├── power_manager.cpp        # Idle timeout, sleep, reboot, shutdown
│   │   ├── audio_es8311.cpp         # ES8311 codec — volume, mic, I2S streams, sound effects
│   │   ├── wifi_service.cpp         # WiFi scan, connect, save credentials
│   │   ├── ble_service.cpp          # NimBLE BLE enable/disable
│   │   ├── ble_uart_service.cpp     # BLE UART (NUS-style) TX/RX
│   │   ├── time_service.cpp         # PCF85063 RTC read, SNTP sync
│   │   ├── imu_qmi8658.cpp          # QMI8658 accel/gyro read
│   │   ├── sdcard_service.cpp       # SDMMC mount, status, TCA9554 power sequencing
│   │   ├── storage_service.cpp      # Internal FAT (/storage) mount
│   │   ├── ota_service.cpp          # HTTPS OTA update from URL
│   │   ├── app_manager.cpp          # .app file scan, install, uninstall, launch
│   │   ├── fileserver_service.cpp   # HTTP file server (SoftAP + port 80)
│   │   └── pc_connect_service.cpp   # USB/VBUS connect detection
│   └── third_party/
│       └── minimp3/minimp3.h        # Lightweight MP3 decoder
├── tools/
│   ├── app_builder.py               # CLI tool to create .app binary packages
│   ├── Clock.app                    # Example clock app
│   ├── Settings.app                 # Example settings app
│   └── Snake.app                    # Example Snake game app
├── CMakeLists.txt                   # Root — adds local component dirs
├── partitions.csv                   # Flash partition layout
├── sdkconfig.defaults               # Build-time defaults (PSRAM, LVGL, WiFi, BLE, …)
├── dependencies.lock                # Managed component lockfile
├── logo.png                         # Boot splash (embedded in firmware)
├── build-flash.ps1                  # Windows: build + flash
├── flash-only.ps1                   # Windows: flash only
├── clean-build-flash.ps1            # Windows: clean build + flash
└── monitor.ps1                      # Windows: serial monitor
```

---

## Architecture Overview

```
app_main()
  └─ wifi_manager_init()            — Web-based WiFi config portal
  └─ radio_player_init()            — Streaming radio engine
  └─ boot_service_init()
       ├─ nvs_flash_init()
       ├─ app_i2c_init()            — Single i2c_master_bus for ALL devices
       ├─ display_lvgl_init()       — SH8601 QSPI + LVGL tick task
       ├─ audio_es8311_init()       — ES8311 codec + I2S
       ├─ Splash screen (logo.png)
       └─ Background services
            ├─ power_axp2101_init()
            ├─ time_service_init()  — PCF85063 RTC
            ├─ imu_qmi8658_init()
            ├─ ble_service_init()
            ├─ wifi_service_init()
            ├─ sdcard_service_mount()
            ├─ storage_service_mount()
            ├─ app_manager_init()   — Scans /storage/apps + /sdcard/apps
            └─ power_manager_init()
  └─ ui_app_carousel_init()         — Main UI
```

The **I2C bus** is created once (`i2c_new_master_bus`) and shared by all devices via `app_i2c_bus()`. Each driver adds its own device handle at init time — no locking needed (new driver is thread-safe).

---

## Services

All services live in `main/services/` and expose a clean C API via headers in `main/include/services/`.

| Service | Key API |
|---------|---------|
| `power_axp2101` | `power_axp2101_get_battery_percent()`, `power_axp2101_is_charging()` |
| `power_manager` | `power_manager_set_idle_timeout_sec()`, `power_manager_sleep_now()` |
| `audio_es8311` | `audio_es8311_set_volume()`, `audio_es8311_play_beep()`, `audio_es8311_stream_begin()` |
| `wifi_service` | `wifi_service_connect()`, `wifi_service_scan()`, `wifi_service_is_connected()` |
| `ble_service` | `ble_service_init()`, `ble_service_set_enabled()` |
| `ble_uart_service` | `ble_uart_service_send()` |
| `time_service` | `time_service_get_localtime()`, `time_service_start_sntp()` |
| `imu_qmi8658` | `imu_qmi8658_read(ax, ay, az, gx, gy, gz)` |
| `sdcard_service` | `sdcard_service_mount()`, `sdcard_service_is_mounted()` |
| `storage_service` | `storage_service_mount()`, `storage_service_mount_point()` |
| `ota_service` | `ota_service_start_from_url(url)` |
| `app_manager` | `app_manager_scan()`, `app_manager_install()`, `app_manager_start()` |
| `fileserver_service` | `fileserver_service_start()`, `fileserver_service_ap_ssid()` |

---

## App System

### Built-in Apps

| App | File | Description |
|-----|------|-------------|
| Settings | `ui_launcher.cpp` | WiFi, display, BLE, OTA, time, power settings |
| Terminal | `ui_terminal.cpp` | Serial-style shell screen |
| Media | `ui_media.cpp` | JPEG/GIF/MP3 viewer |
| MP3 Player | `ui_mp3.cpp` | Full MP3 player with file browser |
| Radio | `ui_radio.cpp` | Internet radio streaming |
| File Server | `ui_fileserver.cpp` | Browse & manage /storage and /sdcard |

### Adding a New Built-in App

**1. Create your UI files**

`main/include/myapp.h`:
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

`main/myapp.cpp` — minimum structure:
```cpp
#include "myapp.h"
#include "lvgl.h"
#include "ui_app_carousel.h"  // for returning to carousel
#include "app_pins.h"

esp_err_t ui_myapp_init(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);

    // Exit button — always provide a way back!
    lv_obj_t *btn = lv_btn_create(screen);
    lv_obj_set_size(btn, 60, 32);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 4, 6);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(lbl);
    lv_obj_add_event_cb(btn, [](lv_event_t *) {
        ui_app_carousel_init();
    }, LV_EVENT_CLICKED, NULL);

    // Your content here ...

    lv_scr_load(screen);
    return ESP_OK;
}
```

**2. Register in the carousel** (`main/ui_app_carousel.cpp`):

- Add `static app_metadata_t s_builtin_myapp;` to static vars
- Populate metadata in `register_builtin_apps()`, increment `s_total_apps`
- Add `if (index == N) return &s_builtin_myapp;` in `get_app_at_index()`
- Add `else if (s_current_app_index == N) { ui_myapp_init(); }` in `launch_current_app()`

**3. Add to `main/CMakeLists.txt`** — append `"myapp.cpp"` to the `SRCS` list.

**4. Include header** in `ui_app_carousel.cpp`.

### Loadable .app Files

Apps can be distributed as self-contained `.app` binary packages and installed to `/storage/apps/` or `/sdcard/apps/` without reflashing.

**App binary format:**

```
Offset    Size    Field
────────────────────────────────────────
0         4       Magic: 0x41505032 ("APP2")
4         4       Version
8         32      Name (null-terminated)
40        32      Creator (null-terminated)
72        16      Category (null-terminated)
88        4       Total file size
92        4       Code offset
96        4       Code size
100       4       Icon offset
104       4       Icon size
108       4       CRC32 checksum
112       32      Reserved
144       32768   Icon data (128×128 RGB565)
32912     var     Executable code
```

**Create a `.app` file with the builder tool:**
```bash
python tools/app_builder.py "MyGame" "D31337m3" "game"
# → MyGame.app
```

Copy the `.app` file to `/storage/apps/` or `/sdcard/apps/`, then use the App Manager in Settings to install it.

---

## Building & Flashing

### Prerequisites

- [ESP-IDF v6.x](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) installed and on `$PATH`
- ESP32-S3 board connected via USB Serial/JTAG

### Linux / macOS

```bash
# Source ESP-IDF environment
source $IDF_PATH/export.sh

cd 07_DEVICE_LAUNCHER

# Build
idf.py build

# Flash (auto-detects port)
idf.py flash

# Flash + monitor
idf.py flash monitor

# Monitor only
idf.py monitor
```

### Windows (PowerShell)

```powershell
cd 07_DEVICE_LAUNCHER

.\build-flash.ps1        # Build + flash
.\flash-only.ps1         # Flash only (binary already built)
.\clean-build-flash.ps1  # Full clean rebuild + flash
.\monitor.ps1            # Open serial monitor
```

### Manual flash (esptool)

```bash
python -m esptool --chip esp32s3 --port /dev/ttyUSB0 --baud 460800 \
  write_flash \
  0x0      build/bootloader/bootloader.bin \
  0x8000   build/partition_table/partition-table.bin \
  0xf000   build/ota_data_initial.bin \
  0x20000  build/device_launcher.bin
```

---

## Configuration

Key `sdkconfig.defaults` settings (override with `idf.py menuconfig`):

| Key | Value | Notes |
|-----|-------|-------|
| `CONFIG_IDF_TARGET` | `esp32s3` | Target chip |
| `CONFIG_ESPTOOLPY_FLASHSIZE_16MB` | `y` | 16 MB flash |
| `CONFIG_SPIRAM_MODE_OCT` | `y` | Octal PSRAM |
| `CONFIG_SPIRAM_SPEED_80M` | `y` | PSRAM @ 80 MHz |
| `CONFIG_LV_COLOR_DEPTH_16` | `y` | RGB565 |
| `CONFIG_LV_COLOR_16_SWAP` | `y` | Byte-swap for SPI panels |
| `CONFIG_LV_MEM_CUSTOM` | `y` | LVGL uses esp heap |
| `CONFIG_LV_USE_PNG` | `y` | PNG decoder (boot splash) |
| `CONFIG_LV_USE_GIF` | `y` | GIF support |
| `CONFIG_LV_USE_SJPG` | `y` | JPEG support |
| `CONFIG_BT_NIMBLE_ENABLED` | `y` | NimBLE BLE stack |
| `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` | `y` | USB JTAG logging |
| `CONFIG_PMU_I2C_SCL` | `14` | AXP2101 SCL pin |
| `CONFIG_PMU_I2C_SDA` | `15` | AXP2101 SDA pin |

---

## LVGL Notes

### Display specs
- Resolution: **368 × 448** (portrait)
- Color depth: **16-bit RGB565**, byte-swapped
- LVGL version: **8.4.x**

### Available fonts
```c
lv_font_montserrat_12
lv_font_montserrat_14  (default)
lv_font_montserrat_16
lv_font_montserrat_20
lv_font_montserrat_24
```

### Display locking — **always required** when modifying LVGL objects from a non-LVGL task:
```cpp
if (display_lvgl_lock(-1)) {
    // safe to create/modify LVGL objects here
    display_lvgl_unlock();
}
```

### Screen constants
```cpp
APP_LCD_H_RES  // 368 — display width
APP_LCD_V_RES  // 448 — display height
```

### LVGL parent/child API — ⚠️ do NOT use direct struct member access:
```cpp
// ❌ WRONG — causes crashes
obj->parent;
parent->child[0];

// ✅ CORRECT
lv_obj_get_parent(obj);
lv_obj_get_child(parent, 0);
lv_event_get_target(event);
```

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Black screen on boot | PMU init fail / I2C not ready | Check SCL=14, SDA=15 in sdkconfig; ensure `app_i2c_init()` called once before `display_lvgl_init()` |
| `fatal error: driver/gpio.h` | Missing `esp_driver_gpio` in component REQUIRES | Add `esp_driver_gpio` to offending component's CMakeLists.txt |
| `esp_lcd_i2c_bus_handle_t` undeclared | Legacy I2C type used with v6 API | Use `i2c_master_bus_handle_t` from `driver/i2c_master.h` |
| Missing field initializers | New struct fields added in ESP-IDF v6 | Zero-init struct or explicitly set new fields (see `esp_lcd_panel_dev_config_t`) |
| SD card mount freezes UI | Blocking mount in UI callback | Mount via `lv_timer_create()` one-shot; update UI only if screen still active |
| Fonts not found | Using unsupported size | Only sizes 12, 14, 16, 20, 24 are compiled |
| Flash size mismatch | `sdkconfig` stale after partition changes | Delete `sdkconfig` and rebuild |
| OTA fails | Partition too small for new binary | Check partition sizes in `partitions.csv`; current firmware fits in 5 MB slot |

---

## Roadmap

- [ ] Boot sound (spray can rattle on splash screen)
- [ ] SD card USB mass-storage mode for easy app uploads
- [ ] App sandbox — crash counting, auto-uninstall on repeated crashes
- [ ] Hardware button context menu (Start/Stop, Uninstall, About, Move, Transfer)
- [ ] Built-in Clock / Alarm app
- [ ] Built-in Calculator
- [ ] Built-in Calendar / Agenda
- [ ] ChatGPT voice assistant interface
- [ ] Zelda-style game demo

---

## Component Dependencies

Managed via `idf_component.yml` + `dependencies.lock`:

| Component | Version | Source |
|-----------|---------|--------|
| `lvgl/lvgl` | 8.4.x | IDF Component Registry |
| `espressif/esp_io_expander_tca9554` | ^1.0.1 | IDF Component Registry |
| `espressif/es8311` | ^1.0.0 | IDF Component Registry |
| `esp_lcd_touch_ft5x06` | latest | IDF Component Registry |
| `esp_lcd_sh8601` | local | `../05_LVGL_WITH_RAM/components/` |
| `XPowersLib` | local | `../01_AXP2101/components/` |
| `SensorLib` | local | `../03_QMI8658/components/` |
| `sd_card` | local | `../04_SD_MMC/components/` |

---

## License

Source code in `main/` is original work. Third-party components retain their respective licenses:
- ESP-IDF — Apache 2.0
- LVGL — MIT
- XPowersLib — MIT
- SensorLib — MIT
- minimp3 — CC0 / public domain

---

*Hardware: ESP32-S3 Touch AMOLED 1.8" — Firmware: Device Launcher v1.0 — ESP-IDF: v6.x*

## Hardware Specifications

### Board: ESP32-S3 Touch AMOLED 1.8"
- **MCU**: ESP32-S3 (Xtensa dual-core @ 240MHz)
- **Flash**: 16MB
- **PSRAM**: 8MB (Octal SPI, 80MHz)
- **Display**: 368x448 SH8601 AMOLED (QSPI interface)
- **Touch**: FT5x06 capacitive touch controller (I2C)
- **PMU**: AXP2101 power management (I2C)
- **IMU**: QMI8658 6-axis accelerometer/gyroscope (I2C)
- **Audio**: ES8311 audio codec (I2S + I2C)
- **SD Card**: MicroSD via SDMMC (1-bit mode)
- **Connectivity**: WiFi 802.11 b/g/n, Bluetooth 5.0 (BLE via NimBLE)

## Pin Configuration

### Display (SH8601 AMOLED - QSPI)
```
GPIO 4  - DATA0
GPIO 5  - DATA1
GPIO 6  - DATA2
GPIO 7  - DATA3
GPIO 11 - CLK
GPIO 12 - CS
```

### I2C Bus (Shared by Touch, PMU, Audio, IMU)
```
GPIO 14 - SCL (200kHz clock speed)
GPIO 15 - SDA

Devices on bus:
  - FT5x06 Touch Controller
  - AXP2101 PMU (Power Management Unit)
  - TCA9554 IO Expander (display power sequencing)
  - QMI8658 IMU
  - ES8311 Audio Codec
```

### SD Card (SDMMC - 1-bit mode)
```
GPIO 1 - CMD
GPIO 2 - CLK
GPIO 3 - DATA0
```

### Audio (ES8311 - I2S)
```
GPIO 8  - I2S_BCK (Bit Clock)
GPIO 9  - I2S_WS (Word Select)
GPIO 10 - I2S_DO (Data Out)
```

### USB Serial/JTAG
- Built-in USB-to-Serial (COM6 on Windows)
- Used for programming, debugging, and serial monitor

## Partition Table (16MB Flash)

| Name     | Type | SubType | Offset    | Size  | Description                    |
|----------|------|---------|-----------|-------|--------------------------------|
| nvs      | data | nvs     | 0x9000    | 24KB  | Non-volatile storage           |
| otadata  | data | ota     | 0xf000    | 8KB   | OTA data partition             |
| phy_init | data | phy     | 0x11000   | 4KB   | PHY init data                  |
| ota_0    | app  | ota_0   | 0x20000   | 5MB   | OTA app partition 0            |
| ota_1    | app  | ota_1   | 0x1c0000  | 5MB   | OTA app partition 1            |
| storage  | data | fat     | 0x360000  | 5MB   | FAT filesystem for SD card     |

Current binary size: ~1.5MB (70% free space in OTA partitions)

## Critical Configuration Notes

### I2C Bus Initialization
⚠️ **IMPORTANT**: The I2C bus MUST be initialized only ONCE in `main.cpp`. 
- Do NOT initialize I2C in `display_lvgl.cpp` or any other file
- All I2C devices (Touch, PMU, IMU, Audio) share the same bus
- Multiple initializations will cause `ESP_FAIL` errors

### PMU Configuration (AXP2101)
The PMU requires specific I2C pin configuration in `sdkconfig.defaults`:
```
CONFIG_PMU_I2C_SCL=14
CONFIG_PMU_I2C_SDA=15
CONFIG_PMU_INTERRUPT_PIN=-1
```
Without this, the display will not power on properly (black screen on boot).

### LVGL Object Access
⚠️ **CRITICAL**: Do NOT use direct pointer access for LVGL parent/child relationships:
- ❌ WRONG: `obj->parent` or `parent->child[0]`
- ✅ CORRECT: Use API functions:
  - `lv_obj_get_parent(obj)`
  - `lv_obj_get_child(parent, index)`
  - `lv_event_get_target(event)`

Direct access causes crashes and undefined behavior.

### SD Card Mounting
SD card mount operations are BLOCKING and freeze LVGL rendering if called directly in UI callbacks.
- ✅ Load screen first, then mount asynchronously via timer
- ✅ Check if screen is still active before updating UI after mount

### WiFi Scanning
- Uses blocking scan (100-300ms)
- Results sorted by RSSI (strongest first)
- Returns simplified structure to reduce memory usage

## Build and Flash

### Prerequisites
- ESP-IDF v5.5.1
- Python 3.13
- COM6 USB Serial/JTAG connection

### Quick Build & Flash
```powershell
# Standard build and flash (recommended for daily development)
.\build-flash.ps1

# Flash only (if binary already built)
.\flash-only.ps1

# Full clean build
.\clean-build-flash.ps1

# Open serial monitor
.\monitor.ps1
```

### Manual Build & Flash
```bash
idf.py build
idf.py -p COM6 flash
idf.py -p COM6 monitor
```

## Project Structure

```
07_DEVICE_LAUNCHER/
├── main/
│   ├── main.cpp                 # Entry point, boot sequence, splash screens
│   ├── ui_launcher.cpp          # Main UI, menus, settings screens
│   ├── display_lvgl.cpp         # Display & LVGL initialization
│   ├── display_lvgl.h           # Display control functions
│   ├── i2c_bus.cpp              # I2C bus initialization
│   ├── services/
│   │   ├── power_axp2101.cpp    # PMU service
│   │   ├── wifi_service.cpp     # WiFi scanning & connection
│   │   ├── ble_service.cpp      # Bluetooth LE service
│   │   ├── imu_qmi8658.cpp      # IMU sensor service
│   │   ├── audio_es8311.cpp     # Audio codec service
│   │   └── time_service.cpp     # SNTP time sync
│   └── CMakeLists.txt           # Embeds logo.png
├── logo.png                     # Boot splash image (graffiti "D31337m3")
├── partitions.csv               # Partition table definition
├── sdkconfig.defaults           # Default build configuration
├── build-flash.ps1              # Build & flash script
├── flash-only.ps1               # Flash only script
├── clean-build-flash.ps1        # Clean build & flash script
└── monitor.ps1                  # Serial monitor script
```

## Features

### Main Launcher Interface
- Touch-based navigation
- Settings menu with all device functions
- On-screen keyboard for text entry (WiFi passwords, OTA URLs, etc.)

### WiFi Settings
- Network scanner showing 10 strongest signals
- Security indicators (🔓 open, 🔒 secured)
- Signal strength display
- One-tap network selection
- Auto-fill SSID from scanned networks
- Connect with password entry

### Display Settings
- Brightness control (10-100%)
- Sleep timeout (15s, 30s, 1min, 2min, 5min, Never)
- Wake on touch/button (checkbox)

### OTA Updates
- Over-the-Air firmware updates
- Dual partition support (ota_0 / ota_1)
- URL-based update with progress indication

### Boot Sequence
1. **Boot Splash** (2 seconds) - Shows logo.png centered on black background
2. **Firmware Update Success** (3 seconds, only after new flash) - Green screen with checkmark
3. **Main Launcher** - Settings menu loads

The firmware tracks builds using NVS storage to detect when a new firmware has been flashed.

## Troubleshooting

### Black Screen on Boot
**Symptoms**: Display stays black, no UI visible
**Causes**:
1. PMU not configured properly in `sdkconfig.defaults`
2. I2C initialized multiple times (check for duplicate `app_i2c_init()` calls)
3. Display power sequencing issue

**Solutions**:
- Verify PMU config in `sdkconfig.defaults` (GPIO 14/15)
- Check serial monitor for I2C driver install errors
- Ensure I2C initialized only once in `main.cpp`

### WiFi Networks Not Selectable
**Cause**: Incorrect parent/child object access in LVGL callbacks
**Solution**: Not fixed yet, must solve asap

### SD Card Menu Blacks Out Display
**Cause**: Blocking mount operation freezes LVGL rendering
**Status**: Attempted fix but still not working - Attempted fix uses async timer-based mounting.
            sd card also needs to be accessable via usb if possible, for easy uploading of apps.

### Build Fails: Partition Too Small
**Symptoms**: `error: overflow` messages during build
**Solution**: Partition table already configured for 16MB flash with 5MB OTA partitions

### Flash Size Mismatch
**Symptoms**: Partition table won't fit, CMake errors
**Solution**: Delete `sdkconfig` file and rebuild to regenerate from `sdkconfig.defaults`

## LVGL Configuration

### Fonts Enabled
- `lv_font_montserrat_12` ✓
- `lv_font_montserrat_14` ✓
- `lv_font_montserrat_16` ✓

### Display Settings
- Resolution: 368x448
- Color depth: 16-bit (RGB565)
- Color swap: Enabled
- Refresh period: 4ms
- Input device read period: 4ms

### Memory
- Uses PSRAM for framebuffers
- Custom memory allocation
- IRAM optimization for fast functions

## Known Issues & Notes

1. **Keyboard Behavior**: Single global keyboard instance reused across text areas. Must be cleaned up when changing screens.

2. **SNTP Time Sync**: Brief screen flash when "Sync Time" is clicked - this is normal LVGL behavior, not a bug.

3. **Font Limitations**: Only sizes 12, 14, and 16 are compiled. Using other sizes will cause build errors.

4. **Deprecated API Warning**: `esp_ota_get_app_description()` shows deprecation warning - functional but should eventually migrate to `esp_app_get_description()`.

## Development Tips

### Adding New Menu Items
1. Add forward declaration at top of `ui_launcher.cpp`
2. Create `open_xxx()` function with screen setup
3. Add menu item in `open_settings()` function
4. Follow existing patterns for keyboard, back button, etc.

### Modifying Partitions
1. Edit `partitions.csv`
2. Ensure total size fits within 16MB
3. Clean build after partition changes

### Adding New Images
1. Place image in project root
2. Add to `EMBED_FILES` in `main/CMakeLists.txt`
3. Declare external symbols in code:
   ```cpp
   extern const uint8_t image_png_start[] asm("_binary_image_png_start");
   extern const uint8_t image_png_end[]   asm("_binary_image_png_end");
   ```
4. Enable image decoder in `sdkconfig.defaults` if needed (PNG already enabled)

### Serial Monitor Commands
Connect via `.\monitor.ps1` or `idf.py monitor` to view:
- Boot sequence logs
- WiFi connection status
- Touch events
- I2C communication
- Error messages
- PMU status

Exit monitor: `Ctrl+]`

## License

Based on ESP-IDF examples and component libraries. Check individual component licenses.

## Support

For hardware documentation, refer to parent directory READMEs:
- `01_AXP2101/` - PMU/Power management
- `02_Touch/` - Touch controller
- `03_QMI8658/` - IMU sensor
- `04_SD_MMC/` - SD card interface
- `05_LVGL_WITH_RAM/` - Display driver
- `06_ES8311/` - Audio codec

---
**Last Updated**: 2026-02-17
**Firmware Version**: 1.0
**ESP-IDF Version**: v5.5.1



****** TODO: ******

1. add a short bootup sound to play when splash screen is displayed.(spraypaint can rattle)
2. create a system for apps. 
	a) loading/starting apps 
	b) managing apps - install/uninstall
	c) loading apps from sdcard
	d) design first demo app for testing - spotify music player / or other streaming service
	e) some sort of game, Zelda for gbc style
3. design/intergrate with current main app a proper app selection menu that appears at launch.

	a) app logos are displayed as one larger app logo in center of app selector with half sized and only half visible on right and left hand sides of middle/selected app.
		i) app logos/metadata are stored in application header.
		ii) app meta data includes, app name, creator, category, and app size, 
	b) app list needs to be swipeable, with selected/highlighted app always displayed center.
	c) app launcher needs to make sure that apps are sandboxed in such a way that if an app crashes it can be terminated and user can still regain access to 
	   to menu. Apps that continuously crash past a certain threshold will be uninstalled till updated with fixes.
	d) app launcher needs a context menu accessable by one of the two hardware bhttons, that shows a menu with:
		i) Start/Stop depending on apps state.
		ii) uninstall
		iii) about
		1v) move - to reposition in menu 
		iiv) transfer - to transfer from sdcard/flash to flash/sdcard		


	e) builtin/system app "apps" including:
		i) clock, alarm clock
		ii) media viewer - that displays/plays jpg, gifs, mp3s
		iii) calculator
		iv) calander/agenda
		v) chatgpt interface for voice assisted ai chats 

4. all changes must be made in. way that can be rolled back in event of crashes, or firmware build errors, update new code in large "patches" and flash to test before adding more patches so buggy code is easier to fix and trace. 


		
	 