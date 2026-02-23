cmake_minimum_required(VERSION 3.16)

# Reuse local components from this demo pack
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../01_AXP2101/components/XPowersLib"
    "${CMAKE_CURRENT_LIST_DIR}/../03_QMI8658/components/SensorLib"
    "${CMAKE_CURRENT_LIST_DIR}/../04_SD_MMC/components/sd_card"
    "${CMAKE_CURRENT_LIST_DIR}/../05_LVGL_WITH_RAM/components/esp_lcd_sh8601"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options("-Wno-format" "-Wno-missing-field-initializers" "-Wno-stringop-truncation")
project(device_launcher)
