#pragma once

#include <stdint.h>

#include "esp_err.h"

// ESP32-S3 GPIO plan for the first removable-wire LCD test.
#define LCD_PIN_SCLK 12
#define LCD_PIN_MOSI 11
#define LCD_PIN_MISO 13
#define LCD_PIN_CS   10
#define LCD_PIN_DC    9
#define LCD_PIN_RST  14

// Final enclosure orientation: module rotated counter-clockwise, header on left.
#define LCD_WIDTH   320
#define LCD_HEIGHT  240

#define LCD_RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

esp_err_t lcd_driver_init(void);
void lcd_fill_screen(uint16_t color);
void lcd_fill_rect(int x, int y, int width, int height, uint16_t color);
void lcd_draw_text(int x, int y, const char *text, uint16_t foreground,
                   uint16_t background, int scale);
