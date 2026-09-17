#pragma once

#include <stdint.h>

void lcd_test_ui_show(void);
void lcd_test_ui_update(uint8_t cpu_percent, uint8_t memory_percent, uint32_t frame_number);
