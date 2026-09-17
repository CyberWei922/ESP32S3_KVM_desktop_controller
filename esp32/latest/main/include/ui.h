#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_state.h"
#include "esp_err.h"

esp_err_t ui_start(app_state_t *state);
bool ui_wait_ready(uint32_t timeout_ms);
esp_err_t ui_set_boot_status(const char *status);
esp_err_t ui_show_main_screen(void);
