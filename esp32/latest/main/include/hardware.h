#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t hardware_safe_init(void);
esp_err_t hardware_set_backlight(bool enabled);
bool hardware_backlight_enabled(void);
esp_err_t hardware_pulse_usb_relay(unsigned duration_ms);
esp_err_t hardware_set_cooler_relay(bool closed);
