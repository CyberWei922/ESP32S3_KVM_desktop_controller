#include "lcd_test_ui.h"

#include <stdio.h>

#include "lcd_driver.h"

static const uint16_t COLOR_BLACK = LCD_RGB565(0, 0, 0);
static const uint16_t COLOR_WHITE = LCD_RGB565(255, 255, 255);
static const uint16_t COLOR_GRAY = LCD_RGB565(45, 52, 64);
static const uint16_t COLOR_GREEN = LCD_RGB565(52, 211, 153);
static const uint16_t COLOR_CYAN = LCD_RGB565(34, 211, 238);

static void draw_progress_bar(int x, int y, int width, uint8_t percent, uint16_t color)
{
    const int height = 12;
    const int inner_width = width - 4;

    lcd_fill_rect(x, y, width, height, COLOR_GRAY);
    lcd_fill_rect(x + 2, y + 2, inner_width, height - 4, COLOR_BLACK);
    lcd_fill_rect(x + 2, y + 2, (inner_width * percent) / 100, height - 4, color);
}

void lcd_test_ui_show(void)
{
    lcd_fill_screen(COLOR_BLACK);
    lcd_fill_rect(0, 0, 80, 4, LCD_RGB565(255, 0, 0));
    lcd_fill_rect(80, 0, 80, 4, LCD_RGB565(0, 255, 0));
    lcd_fill_rect(160, 0, 80, 4, LCD_RGB565(0, 0, 255));
    lcd_fill_rect(240, 0, 80, 4, COLOR_WHITE);

    lcd_draw_text(12, 14, "KVM CONTROL", COLOR_WHITE, COLOR_BLACK, 2);
    lcd_draw_text(200, 14, "LIVE 00000", COLOR_GREEN, COLOR_BLACK, 2);
    lcd_fill_rect(10, 38, 300, 2, COLOR_GRAY);

    lcd_draw_text(14, 52, "MAC DEMO", COLOR_GREEN, COLOR_BLACK, 2);
    lcd_draw_text(14, 78, "CPU: 000%", COLOR_WHITE, COLOR_BLACK, 2);
    draw_progress_bar(14, 99, 180, 0, COLOR_GREEN);
    lcd_draw_text(14, 124, "MEM: 000%", COLOR_WHITE, COLOR_BLACK, 2);
    draw_progress_bar(14, 145, 180, 0, COLOR_CYAN);

    lcd_fill_rect(208, 48, 2, 140, COLOR_GRAY);
    lcd_draw_text(220, 56, "WINDOWS", COLOR_CYAN, COLOR_BLACK, 2);
    lcd_draw_text(220, 82, "OFFLINE", COLOR_WHITE, COLOR_BLACK, 2);
    lcd_draw_text(220, 112, "2 FPS", COLOR_GREEN, COLOR_BLACK, 2);

    lcd_fill_rect(10, 198, 300, 2, COLOR_GRAY);
    lcd_draw_text(82, 214, "LCD 320 X 240", COLOR_WHITE, COLOR_BLACK, 2);
    lcd_fill_rect(0, 236, 320, 4, COLOR_WHITE);
}

void lcd_test_ui_update(uint8_t cpu_percent, uint8_t memory_percent, uint32_t frame_number)
{
    char text[16];

    if (cpu_percent > 100) cpu_percent = 100;
    if (memory_percent > 100) memory_percent = 100;

    snprintf(text, sizeof(text), "CPU: %03u%%", (unsigned int)cpu_percent);
    lcd_draw_text(14, 78, text, COLOR_WHITE, COLOR_BLACK, 2);
    draw_progress_bar(14, 99, 180, cpu_percent, COLOR_GREEN);

    snprintf(text, sizeof(text), "MEM: %03u%%", (unsigned int)memory_percent);
    lcd_draw_text(14, 124, text, COLOR_WHITE, COLOR_BLACK, 2);
    draw_progress_bar(14, 145, 180, memory_percent, COLOR_CYAN);

    snprintf(text, sizeof(text), "LIVE %05lu", (unsigned long)(frame_number % 100000));
    lcd_draw_text(200, 14, text, COLOR_GREEN, COLOR_BLACK, 2);
}
