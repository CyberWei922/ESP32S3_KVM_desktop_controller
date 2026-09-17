#pragma once

#include <stdint.h>

#include "app_state.h"

void lcd_test_ui_show(void);
void lcd_test_ui_update(const app_state_snapshot_t *snapshot, uint32_t frame_number);
void lcd_test_ui_update_buttons(uint8_t pressed_mask);
