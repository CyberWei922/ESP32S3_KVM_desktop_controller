#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#define BUTTON_PIN_LEFT   4
#define BUTTON_PIN_MIDDLE 5
#define BUTTON_PIN_RIGHT  6

#define BUTTON_MASK_LEFT   (1U << 0)
#define BUTTON_MASK_MIDDLE (1U << 1)
#define BUTTON_MASK_RIGHT  (1U << 2)

esp_err_t buttons_init(void);
bool buttons_poll(int64_t now_ms, uint8_t *pressed_mask);
