#include "lcd_test_ui.h"

#include "lcd_driver.h"

void lcd_test_ui_show(void)
{
    const uint16_t black = LCD_RGB565(0, 0, 0);
    const uint16_t white = LCD_RGB565(255, 255, 255);
    const uint16_t gray = LCD_RGB565(45, 52, 64);
    const uint16_t green = LCD_RGB565(52, 211, 153);
    const uint16_t cyan = LCD_RGB565(34, 211, 238);

    lcd_fill_screen(black);
    lcd_fill_rect(0, 0, 240, 4, LCD_RGB565(255, 0, 0));
    lcd_fill_rect(0, 4, 240, 4, LCD_RGB565(0, 255, 0));
    lcd_fill_rect(0, 8, 240, 4, LCD_RGB565(0, 0, 255));
    lcd_fill_rect(0, 12, 240, 4, white);

    lcd_draw_text(30, 30, "KVM CONTROL", white, black, 3);
    lcd_fill_rect(12, 72, 216, 2, gray);

    lcd_draw_text(18, 92, "MAC", green, black, 2);
    lcd_draw_text(18, 118, "CPU: 48.5 C", white, black, 2);
    lcd_draw_text(18, 142, "MEM: 50%", white, black, 2);

    lcd_fill_rect(12, 174, 216, 2, gray);
    lcd_draw_text(18, 194, "WINDOWS", cyan, black, 2);
    lcd_draw_text(18, 220, "OFFLINE", white, black, 2);

    lcd_fill_rect(12, 254, 216, 2, gray);
    lcd_draw_text(18, 274, "LCD TEST OK", green, black, 2);
    lcd_fill_rect(0, 316, 240, 4, white);
}
