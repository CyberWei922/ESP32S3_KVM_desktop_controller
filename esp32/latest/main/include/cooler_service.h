#pragma once

#include "app_state.h"
#include "button_service.h"
#include "esp_err.h"

esp_err_t cooler_service_start(app_state_t *state);
void cooler_service_handle_event(app_event_type_t event);
