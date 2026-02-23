# App Builder Tool

Python tool for creating `.app` files for the ESP32-S3 Device Launcher.

## Usage

```bash
python app_builder.py <name> <creator> <category>
```

### Parameters

- **name**: App name (max 31 characters)
- **creator**: Creator name (max 31 characters)
- **category**: One of: `system`, `game`, `media`, `productivity`, `utility`, `communication`, `other`

### Examples

```bash
# Create a system app
python app_builder.py "Settings" "D31337m3" "system"

# Create a game
python app_builder.py "Snake" "D31337m3" "game"

# Create a media app
python app_builder.py "Music Player" "D31337m3" "media"
```

## App File Format

The `.app` file format consists of:

### Header (128 bytes)
- Magic number: `0x41505032` ("APP2")
- Version: 2
- Name (32 bytes, null-terminated)
- Creator (32 bytes, null-terminated)
- Category (16 bytes, null-terminated)
- Total size
- Code offset and size
- Icon offset and size
- CRC32 checksum
- Reserved space (32 bytes)

### Icon Data (32,768 bytes)
- 128x128 pixels
- RGB565 format (16-bit color)
- Currently generates gradient pattern for testing

### Code Data (variable size)
- Currently 1KB of dummy data
- Future: Actual compiled application code

## File Structure

```
App File Layout:
┌─────────────────────┐
│ Header (128 bytes)  │  <- App metadata
├─────────────────────┤
│ Icon (32KB)         │  <- RGB565 128x128 icon
├─────────────────────┤
│ Code (variable)     │  <- Application code
└─────────────────────┘
```

## Output

Generated `.app` files can be:
- Copied to SD card in `/sdcard/apps/` directory
- Uploaded to flash storage in `/storage/apps/` directory
- Installed via the device's app manager UI

## Current Limitations

- Icon is auto-generated (gradient pattern)
- Code section is dummy data
- Maximum app size: 500KB
- Maximum 16 apps can be installed

## Future Enhancements

- Custom icon support (from PNG/JPG files)
- Actual code compilation and linking
- App dependencies management
- Digital signatures for app verification
