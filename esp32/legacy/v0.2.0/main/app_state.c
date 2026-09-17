#include "app_state.h"

#include <stdio.h>
#include <string.h>

void app_state_init(app_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->active_host = HOST_MAC;
    state->display_input_confidence = STATE_UNKNOWN;
    state->usb_owner_confidence = STATE_UNKNOWN;
    state->thermal_mode = THERMAL_MODE_NORMAL;
    state->cooler_level_confidence = STATE_UNKNOWN;
    snprintf(state->ip_address, sizeof(state->ip_address), "0.0.0.0");
    snprintf(state->last_message, sizeof(state->last_message), "System initialized");
}

void app_state_load_demo_data(app_state_t *state)
{
    host_status_t *mac = &state->hosts[HOST_MAC];
    mac->online = true;
    mac->temperature_valid = true;
    mac->cpu_temperature_c = 48.5f;
    mac->memory_valid = true;
    mac->memory_used_bytes = UINT64_C(12) * 1024 * 1024 * 1024;
    mac->memory_total_bytes = UINT64_C(24) * 1024 * 1024 * 1024;
    mac->memory_percent = 50.0f;

    host_status_t *windows = &state->hosts[HOST_WINDOWS];
    windows->online = false;
    snprintf(windows->error, sizeof(windows->error), "Waiting for host agent");
}
