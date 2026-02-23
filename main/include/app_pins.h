#pragma once

#include "driver/gpio.h"

// Display: SH8601 over QSPI (matches 05_LVGL_WITH_RAM/main/example_qspi_with_ram.c)
#define APP_PIN_NUM_LCD_CS    (GPIO_NUM_12)
#define APP_PIN_NUM_LCD_PCLK  (GPIO_NUM_11)
#define APP_PIN_NUM_LCD_DATA0 (GPIO_NUM_4)
#define APP_PIN_NUM_LCD_DATA1 (GPIO_NUM_5)
#define APP_PIN_NUM_LCD_DATA2 (GPIO_NUM_6)
#define APP_PIN_NUM_LCD_DATA3 (GPIO_NUM_7)
#define APP_PIN_NUM_LCD_RST   GPIO_NUM_NC
#define APP_PIN_NUM_LCD_BK    GPIO_NUM_NC

#define APP_LCD_H_RES (368)
#define APP_LCD_V_RES (448)

// Touch: FT5x06 on I2C0 (same pins as demo pack)
#define APP_PIN_NUM_I2C_SCL (GPIO_NUM_14)
#define APP_PIN_NUM_I2C_SDA (GPIO_NUM_15)

#define APP_PIN_NUM_TOUCH_RST ((gpio_num_t)-1)
#define APP_PIN_NUM_TOUCH_INT (GPIO_NUM_21)

// Audio: ES8311 + I2S (from 06_I2SCodec/main/example_config.h)
#define APP_PIN_NUM_AUDIO_PA_EN (GPIO_NUM_46)
#define APP_PIN_NUM_I2S_MCLK    (GPIO_NUM_16)
#define APP_PIN_NUM_I2S_BCLK    (GPIO_NUM_9)
#define APP_PIN_NUM_I2S_WS      (GPIO_NUM_45)
#define APP_PIN_NUM_I2S_DOUT    (GPIO_NUM_8)
#define APP_PIN_NUM_I2S_DIN     (GPIO_NUM_10)

// SD Card (SDMMC - 1-bit mode)
#define APP_PIN_NUM_SDMMC_CMD   (GPIO_NUM_1)
#define APP_PIN_NUM_SDMMC_CLK   (GPIO_NUM_2)
#define APP_PIN_NUM_SDMMC_D0    (GPIO_NUM_3)
