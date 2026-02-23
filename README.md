# ESP32-S3 Touch AMOLED 1.8" Device Launcher

A full-featured device launcher/settings interface for the ESP32-S3 Touch AMOLED 1.8" display module with WiFi, Bluetooth, IMU, Audio, SD Card support, and OTA updates.

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


		
	 