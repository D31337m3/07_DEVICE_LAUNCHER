#!/usr/bin/env python3
"""
App Builder Tool - Creates .app files for ESP32-S3 Device Launcher

Usage: python app_builder.py <app_name> <creator> <category>
"""

import struct
import sys
import zlib
import os

APP_MAGIC = 0x41505032  # "APP2"
APP_NAME_MAX_LEN = 32
APP_CREATOR_MAX_LEN = 32
APP_CATEGORY_MAX_LEN = 16
APP_ICON_WIDTH = 128
APP_ICON_HEIGHT = 128

def create_test_icon():
    """Generate a simple test icon (128x128 RGB565)"""
    icon_data = bytearray()
    for y in range(APP_ICON_HEIGHT):
        for x in range(APP_ICON_WIDTH):
            r = (x * 31) // APP_ICON_WIDTH
            g = (y * 63) // APP_ICON_HEIGHT
            b = ((x + y) * 31) // (APP_ICON_WIDTH + APP_ICON_HEIGHT)
            rgb565 = ((r & 0x1F) << 11) | ((g & 0x3F) << 5) | (b & 0x1F)
            icon_data.extend(struct.pack('<H', rgb565))
    return bytes(icon_data)

def create_dummy_code():
    return b'\x00' * 1024

def create_app_file(name, creator, category, output_path=None):
    if output_path is None:
        output_path = f"{name.replace(' ', '_')}.app"
    
    if len(name) >= APP_NAME_MAX_LEN:
        name = name[:APP_NAME_MAX_LEN-1]
    if len(creator) >= APP_CREATOR_MAX_LEN:
        creator = creator[:APP_CREATOR_MAX_LEN-1]
    if len(category) >= APP_CATEGORY_MAX_LEN:
        category = category[:APP_CATEGORY_MAX_LEN-1]
    
    icon_data = create_test_icon()
    code_data = create_dummy_code()
    
    header_size = 128
    icon_offset = header_size
    icon_size = len(icon_data)
    code_offset = icon_offset + icon_size
    code_size = len(code_data)
    total_size = code_offset + code_size
    
    header = bytearray(header_size)
    struct.pack_into('<I', header, 0, APP_MAGIC)
    struct.pack_into('<I', header, 4, 2)
    
    name_bytes = name.encode('utf-8')[:APP_NAME_MAX_LEN-1] + b'\x00'
    creator_bytes = creator.encode('utf-8')[:APP_CREATOR_MAX_LEN-1] + b'\x00'
    category_bytes = category.encode('utf-8')[:APP_CATEGORY_MAX_LEN-1] + b'\x00'
    
    header[8:8+len(name_bytes)] = name_bytes
    header[8+APP_NAME_MAX_LEN:8+APP_NAME_MAX_LEN+len(creator_bytes)] = creator_bytes
    header[8+APP_NAME_MAX_LEN+APP_CREATOR_MAX_LEN:8+APP_NAME_MAX_LEN+APP_CREATOR_MAX_LEN+len(category_bytes)] = category_bytes
    
    offset = 8 + APP_NAME_MAX_LEN + APP_CREATOR_MAX_LEN + APP_CATEGORY_MAX_LEN
    struct.pack_into('<I', header, offset, total_size)
    struct.pack_into('<I', header, offset+4, code_offset)
    struct.pack_into('<I', header, offset+8, code_size)
    struct.pack_into('<I', header, offset+12, icon_offset)
    struct.pack_into('<I', header, offset+16, icon_size)
    
    file_data = bytes(header) + icon_data + code_data
    checksum = zlib.crc32(file_data) & 0xFFFFFFFF
    struct.pack_into('<I', header, offset+20, checksum)
    
    file_data = bytes(header) + icon_data + code_data
    
    with open(output_path, 'wb') as f:
        f.write(file_data)
    
    print(f"Created {output_path}")
    print(f"  Name: {name}")
    print(f"  Creator: {creator}")
    print(f"  Category: {category}")
    print(f"  Size: {len(file_data)} bytes")
    
    return output_path

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python app_builder.py <name> <creator> <category>")
        print("\nCategories: system, game, media, productivity, utility, communication, other")
        sys.exit(1)
    
    name = sys.argv[1]
    creator = sys.argv[2]
    category = sys.argv[3]
    
    create_app_file(name, creator, category)
