#include "app_state.h"

#include <stdio.h>
#include <string.h>

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity == 0) return;
    snprintf(destination, capacity, "%s", source != NULL ? source : "");
}

static bool lock_state(app_state_t *state)
{
    return state != NULL && state->mutex != NULL &&
           xSemaphoreTake(state->mutex, portMAX_DELAY) == pdTRUE;
}

esp_err_t app_state_init(app_state_t *state)
{
    if (state == NULL) return ESP_ERR_INVALID_ARG;
    memset(state, 0, sizeof(*state));
    state->mutex = xSemaphoreCreateMutex();
    if (state->mutex == NULL) return ESP_ERR_NO_MEM;

    state->value.active_host = HOST_MAC;
    state->value.display_input_confidence = STATE_UNKNOWN;
    state->value.usb_owner_confidence = STATE_UNKNOWN;
    state->value.thermal_mode = THERMAL_MODE_NORMAL;
    state->value.cooler_level_confidence = STATE_UNKNOWN;
    copy_text(state->value.ip_address, sizeof(state->value.ip_address), "0.0.0.0");
    copy_text(state->value.last_message, sizeof(state->value.last_message),
              "SYSTEM INITIALIZED");
    for (int i = 0; i < HOST_COUNT; ++i) {
        copy_text(state->value.hosts[i].temperature_error,
                  sizeof(state->value.hosts[i].temperature_error), "offline");
        copy_text(state->value.hosts[i].memory_error,
                  sizeof(state->value.hosts[i].memory_error), "offline");
    }
    return ESP_OK;
}

bool app_state_get_snapshot(app_state_t *state, app_state_snapshot_t *snapshot)
{
    if (snapshot == NULL || !lock_state(state)) return false;
    *snapshot = state->value;
    xSemaphoreGive(state->mutex);
    return true;
}

void app_state_set_wifi_configured(app_state_t *state, bool configured)
{
    if (!lock_state(state)) return;
    state->value.wifi_configured = configured;
    xSemaphoreGive(state->mutex);
}

void app_state_set_wifi(app_state_t *state, bool connected, const char *ip_address,
                        const char *message)
{
    if (!lock_state(state)) return;
    state->value.wifi_connected = connected;
    copy_text(state->value.ip_address, sizeof(state->value.ip_address),
              connected && ip_address != NULL ? ip_address : "0.0.0.0");
    if (message != NULL) {
        copy_text(state->value.last_message, sizeof(state->value.last_message), message);
    }
    xSemaphoreGive(state->mutex);
}

void app_state_set_host_online(app_state_t *state, host_id_t host, const char *device_id,
                               int64_t connected_monotonic_ms)
{
    if (host >= HOST_COUNT || !lock_state(state)) return;
    host_status_t *status = &state->value.hosts[host];
    memset(status, 0, sizeof(*status));
    status->online = true;
    status->last_telemetry_monotonic_ms = connected_monotonic_ms;
    copy_text(status->device_id, sizeof(status->device_id), device_id);
    copy_text(status->temperature_error, sizeof(status->temperature_error), "data_unavailable");
    copy_text(status->memory_error, sizeof(status->memory_error), "data_unavailable");
    xSemaphoreGive(state->mutex);
}

void app_state_set_host_offline(app_state_t *state, host_id_t host, const char *reason)
{
    if (host >= HOST_COUNT || !lock_state(state)) return;
    host_status_t *status = &state->value.hosts[host];
    status->online = false;
    status->stale = false;
    status->temperature_valid = false;
    status->memory_valid = false;
    copy_text(status->temperature_error, sizeof(status->temperature_error),
              reason != NULL ? reason : "offline");
    copy_text(status->memory_error, sizeof(status->memory_error),
              reason != NULL ? reason : "offline");
    xSemaphoreGive(state->mutex);
}

void app_state_commit_telemetry(app_state_t *state, host_id_t host,
                                const host_telemetry_update_t *update)
{
    if (host >= HOST_COUNT || update == NULL || !lock_state(state)) return;
    host_status_t *status = &state->value.hosts[host];
    status->online = true;
    status->stale = false;
    status->temperature_valid = update->temperature_valid;
    status->cpu_temperature_c = update->cpu_temperature_c;
    status->memory_valid = update->memory_valid;
    status->memory_percent = update->memory_percent;
    status->temperature_sampled_at_ms = update->temperature_sampled_at_ms;
    status->memory_sampled_at_ms = update->memory_sampled_at_ms;
    status->snapshot_timestamp_ms = update->snapshot_timestamp_ms;
    status->last_telemetry_monotonic_ms = update->received_monotonic_ms;
    status->sequence = update->sequence;
    copy_text(status->temperature_error, sizeof(status->temperature_error),
              update->temperature_error);
    copy_text(status->memory_error, sizeof(status->memory_error), update->memory_error);
    xSemaphoreGive(state->mutex);
}

uint32_t app_state_mark_stale_hosts(app_state_t *state, int64_t now_monotonic_ms)
{
    if (!lock_state(state)) return 0;
    uint32_t stale_mask = 0;
    for (int i = 0; i < HOST_COUNT; ++i) {
        host_status_t *status = &state->value.hosts[i];
        if (status->online && !status->stale && status->last_telemetry_monotonic_ms > 0 &&
            now_monotonic_ms - status->last_telemetry_monotonic_ms > APP_STATE_STALE_TIMEOUT_MS) {
            status->stale = true;
            status->temperature_valid = false;
            status->memory_valid = false;
            copy_text(status->temperature_error, sizeof(status->temperature_error), "data_stale");
            copy_text(status->memory_error, sizeof(status->memory_error), "data_stale");
            stale_mask |= 1U << i;
        }
    }
    xSemaphoreGive(state->mutex);
    return stale_mask;
}

void app_state_set_last_message(app_state_t *state, const char *message)
{
    if (!lock_state(state)) return;
    copy_text(state->value.last_message, sizeof(state->value.last_message), message);
    xSemaphoreGive(state->mutex);
}

void app_state_set_command_pending(app_state_t *state, const char *request_id)
{
    if (!lock_state(state)) return;
    state->value.command_status = COMMAND_PENDING;
    state->value.command_success = false;
    state->value.command_write_succeeded = false;
    state->value.command_confirmed = false;
    copy_text(state->value.command_request_id, sizeof(state->value.command_request_id), request_id);
    state->value.command_error[0] = '\0';
    xSemaphoreGive(state->mutex);
}

void app_state_set_command_result(app_state_t *state, const char *request_id, bool success,
                                  bool write_succeeded, bool confirmed, const char *error)
{
    if (!lock_state(state)) return;
    state->value.command_status = COMMAND_RESULT;
    state->value.command_success = success;
    state->value.command_write_succeeded = write_succeeded;
    state->value.command_confirmed = confirmed;
    copy_text(state->value.command_request_id, sizeof(state->value.command_request_id), request_id);
    copy_text(state->value.command_error, sizeof(state->value.command_error), error);
    xSemaphoreGive(state->mutex);
}

void app_state_set_command_failed(app_state_t *state, const char *request_id,
                                  command_status_t status, const char *error)
{
    if (status != COMMAND_TIMEOUT && status != COMMAND_DISCONNECTED &&
        status != COMMAND_SEND_FAILED) return;
    if (!lock_state(state)) return;
    state->value.command_status = status;
    state->value.command_success = false;
    state->value.command_write_succeeded = false;
    state->value.command_confirmed = false;
    copy_text(state->value.command_request_id, sizeof(state->value.command_request_id), request_id);
    copy_text(state->value.command_error, sizeof(state->value.command_error), error);
    xSemaphoreGive(state->mutex);
}
