#include "app_state.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"

static void copy_text(char *destination, size_t capacity, const char *source)
{
    if (capacity > 0) snprintf(destination, capacity, "%s", source != NULL ? source : "");
}

static bool lock_state(app_state_t *state)
{
    return state != NULL && state->mutex != NULL &&
           xSemaphoreTake(state->mutex, portMAX_DELAY) == pdTRUE;
}

static void changed(app_state_t *state)
{
    ++state->value.revision;
}

esp_err_t app_state_init(app_state_t *state)
{
    if (state == NULL) return ESP_ERR_INVALID_ARG;
    memset(state, 0, sizeof(*state));
    state->mutex = xSemaphoreCreateMutex();
    if (state->mutex == NULL) return ESP_ERR_NO_MEM;
    state->value.display_target = HOST_MAC;
    state->value.usb_owner = HOST_MAC;
    state->value.display_confidence = CONFIDENCE_UNKNOWN;
    state->value.usb_confidence = CONFIDENCE_UNKNOWN;
    state->value.thermal_mode = THERMAL_MANUAL;
    state->value.cooler_control_enabled = false;
    state->value.cooler_state = COOLER_UNKNOWN;
    state->value.cooler_level = 0;
    state->value.dashboard_source = HOST_MAC;
    state->value.thermal_targets_c[THERMAL_MANUAL] = 0;
    state->value.thermal_targets_c[THERMAL_STANDARD] = 40;
    state->value.thermal_targets_c[THERMAL_SILENT] = 50;
    state->value.thermal_targets_c[THERMAL_TURBO] = 28;
    state->value.kvm_phase = APP_KVM_TIMING_CONFIRMED
        ? KVM_IDLE : KVM_DISABLED_UNCONFIRMED_TIMING;
    copy_text(state->value.ip_address, sizeof(state->value.ip_address), "0.0.0.0");
    copy_text(state->value.last_message, sizeof(state->value.last_message),
              APP_KVM_TIMING_CONFIRMED ? "SYSTEM READY" : "KVM TIMING NOT CONFIRMED");
    for (int i = 0; i < HOST_COUNT; ++i) {
        copy_text(state->value.hosts[i].temperature_error,
                  sizeof(state->value.hosts[i].temperature_error), "offline");
        copy_text(state->value.hosts[i].memory_error,
                  sizeof(state->value.hosts[i].memory_error), "offline");
    }
    state->value.revision = 1;
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
    changed(state);
    xSemaphoreGive(state->mutex);
}

void app_state_set_wifi(app_state_t *state, bool connected, const char *ip_address,
                        const char *message)
{
    if (!lock_state(state)) return;
    state->value.wifi_connected = connected;
    copy_text(state->value.ip_address, sizeof(state->value.ip_address),
              connected && ip_address != NULL ? ip_address : "0.0.0.0");
    if (message != NULL) copy_text(state->value.last_message,
                                   sizeof(state->value.last_message), message);
    changed(state);
    xSemaphoreGive(state->mutex);
}

void app_state_set_host_online(app_state_t *state, host_id_t host, const char *device_id,
                               int64_t now_ms)
{
    if (host >= HOST_COUNT || !lock_state(state)) return;
    host_status_t *status = &state->value.hosts[host];
    memset(status, 0, sizeof(*status));
    status->online = true;
    status->last_update_monotonic_ms = now_ms;
    copy_text(status->device_id, sizeof(status->device_id), device_id);
    copy_text(status->temperature_error, sizeof(status->temperature_error), "data_unavailable");
    copy_text(status->memory_error, sizeof(status->memory_error), "data_unavailable");
    changed(state);
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
    changed(state);
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
    status->last_update_monotonic_ms = update->received_monotonic_ms;
    status->sequence = update->sequence;
    copy_text(status->temperature_error, sizeof(status->temperature_error),
              update->temperature_error);
    copy_text(status->memory_error, sizeof(status->memory_error), update->memory_error);
    changed(state);
    xSemaphoreGive(state->mutex);
}

uint32_t app_state_mark_stale_hosts(app_state_t *state, int64_t now_ms)
{
    if (!lock_state(state)) return 0;
    uint32_t mask = 0;
    for (int i = 0; i < HOST_COUNT; ++i) {
        host_status_t *host = &state->value.hosts[i];
        if (host->online && !host->stale && host->last_update_monotonic_ms > 0 &&
            now_ms - host->last_update_monotonic_ms >= APP_HOST_STALE_TIMEOUT_MS) {
            host->online = false;
            host->stale = true;
            host->temperature_valid = false;
            host->memory_valid = false;
            copy_text(host->temperature_error, sizeof(host->temperature_error), "data_stale");
            copy_text(host->memory_error, sizeof(host->memory_error), "data_stale");
            mask |= 1U << i;
        }
    }
    if (mask != 0) changed(state);
    xSemaphoreGive(state->mutex);
    return mask;
}

void app_state_set_last_message(app_state_t *state, const char *message)
{
    if (!lock_state(state)) return;
    copy_text(state->value.last_message, sizeof(state->value.last_message), message);
    changed(state);
    xSemaphoreGive(state->mutex);
}

void app_state_set_backlight(app_state_t *state, bool enabled)
{
    if (!lock_state(state)) return;
    state->value.backlight_enabled = enabled;
    changed(state);
    xSemaphoreGive(state->mutex);
}

void app_state_next_data_window(app_state_t *state)
{
    if (!lock_state(state)) return;
    state->value.data_window_index = (uint8_t)((state->value.data_window_index + 1) % 3);
    copy_text(state->value.last_message, sizeof(state->value.last_message), "DASHBOARD PAGE CHANGED");
    changed(state);
    xSemaphoreGive(state->mutex);
}

void app_state_previous_data_window(app_state_t *state)
{
    if (!lock_state(state)) return;
    state->value.data_window_index = (uint8_t)((state->value.data_window_index + 2) % 3);
    copy_text(state->value.last_message, sizeof(state->value.last_message), "DASHBOARD PAGE CHANGED");
    changed(state);
    xSemaphoreGive(state->mutex);
}

bool app_state_commit_dashboard(app_state_t *state, host_id_t host,
                                const dashboard_update_t *update)
{
    if (host >= HOST_COUNT || update == NULL || !lock_state(state)) return false;
    const int64_t now = update->received_monotonic_ms;
    bool accept = !state->value.dashboard_valid || state->value.dashboard_stale;

    if (state->value.dashboard_valid && !state->value.dashboard_stale) {
        if (state->value.dashboard_source == HOST_WINDOWS) {
            accept = host == HOST_WINDOWS;
        } else if (host == HOST_MAC) {
            accept = true;
        } else {
            if (state->value.windows_dashboard_recovery_since_ms == 0 ||
                now - state->value.windows_dashboard_candidate_last_ms > 20000) {
                state->value.windows_dashboard_recovery_since_ms = now;
            }
            state->value.windows_dashboard_candidate_last_ms = now;
            accept = now - state->value.windows_dashboard_recovery_since_ms >=
                     APP_DASHBOARD_WINDOWS_RECOVERY_MS;
        }
    }

    if (!accept) {
        xSemaphoreGive(state->mutex);
        return false;
    }
    state->value.dashboard_valid = update->weather.valid || update->markets.valid;
    state->value.dashboard_stale = false;
    state->value.dashboard_source = host;
    state->value.dashboard_received_monotonic_ms = now;
    state->value.dashboard_sequence = update->sequence;
    state->value.weather = update->weather;
    state->value.markets = update->markets;
    copy_text(state->value.dashboard_error, sizeof(state->value.dashboard_error), update->error);
    if (host == HOST_WINDOWS) {
        state->value.windows_dashboard_recovery_since_ms = 0;
        state->value.windows_dashboard_candidate_last_ms = 0;
    }
    ++state->value.dashboard_revision;
    changed(state);
    xSemaphoreGive(state->mutex);
    return true;
}

void app_state_mark_dashboard_stale(app_state_t *state, int64_t now_ms)
{
    if (!lock_state(state)) return;
    if (state->value.dashboard_valid && !state->value.dashboard_stale &&
        state->value.dashboard_received_monotonic_ms > 0 &&
        now_ms - state->value.dashboard_received_monotonic_ms >=
        APP_DASHBOARD_STALE_TIMEOUT_MS) {
        state->value.dashboard_stale = true;
        state->value.windows_dashboard_recovery_since_ms = 0;
        state->value.windows_dashboard_candidate_last_ms = 0;
        copy_text(state->value.dashboard_error, sizeof(state->value.dashboard_error),
                  "dashboard_stale");
        ++state->value.dashboard_revision;
        changed(state);
    }
    xSemaphoreGive(state->mutex);
}

void app_state_next_thermal_mode(app_state_t *state)
{
    if (!lock_state(state)) return;
    state->value.thermal_mode = (thermal_mode_t)((state->value.thermal_mode + 1) %
                                                 THERMAL_MODE_COUNT);
    copy_text(state->value.last_message, sizeof(state->value.last_message),
              "COOLER UI MODE CHANGED");
    changed(state);
    xSemaphoreGive(state->mutex);
}

void app_state_set_thermal_mode(app_state_t *state, thermal_mode_t mode)
{
    if (mode >= THERMAL_MODE_COUNT || !lock_state(state)) return;
    state->value.thermal_mode = mode;
    copy_text(state->value.last_message, sizeof(state->value.last_message),
              "COOLER MODE CHANGED");
    changed(state);
    xSemaphoreGive(state->mutex);
}

int app_state_adjust_thermal_target(app_state_t *state, int delta_c)
{
    if (!lock_state(state)) return 0;
    const thermal_mode_t mode = state->value.thermal_mode;
    if (mode == THERMAL_MANUAL || mode >= THERMAL_MODE_COUNT) {
        xSemaphoreGive(state->mutex);
        return 0;
    }
    int target = state->value.thermal_targets_c[mode] + delta_c;
    if (target < APP_COOLER_TARGET_MIN_C) target = APP_COOLER_TARGET_MIN_C;
    if (target > APP_COOLER_TARGET_MAX_C) target = APP_COOLER_TARGET_MAX_C;
    state->value.thermal_targets_c[mode] = target;
    copy_text(state->value.last_message, sizeof(state->value.last_message),
              "COOLER TARGET CHANGED");
    changed(state);
    xSemaphoreGive(state->mutex);
    return target;
}

void app_state_set_thermal_target(app_state_t *state, thermal_mode_t mode, int target_c)
{
    if (mode <= THERMAL_MANUAL || mode >= THERMAL_MODE_COUNT || !lock_state(state)) return;
    if (target_c < APP_COOLER_TARGET_MIN_C) target_c = APP_COOLER_TARGET_MIN_C;
    if (target_c > APP_COOLER_TARGET_MAX_C) target_c = APP_COOLER_TARGET_MAX_C;
    state->value.thermal_targets_c[mode] = target_c;
    changed(state);
    xSemaphoreGive(state->mutex);
}

void app_state_set_cooler_status(app_state_t *state, cooler_state_t cooler_state,
                                 uint8_t level, bool enabled, const char *message)
{
    if (!lock_state(state)) return;
    state->value.cooler_state = cooler_state;
    state->value.cooler_level = level <= 4 ? level : 0;
    state->value.cooler_control_enabled = enabled;
    if (message != NULL) copy_text(state->value.last_message,
                                   sizeof(state->value.last_message), message);
    changed(state);
    xSemaphoreGive(state->mutex);
}

void app_state_commit_usb_correction(app_state_t *state)
{
    if (!lock_state(state)) return;
    state->value.usb_owner = state->value.display_target;
    state->value.usb_confidence = CONFIDENCE_ESTIMATED;
    copy_text(state->value.last_message, sizeof(state->value.last_message),
              "USB STATE CORRECTED");
    changed(state);
    xSemaphoreGive(state->mutex);
}

void app_state_set_kvm_phase(app_state_t *state, kvm_phase_t phase, const char *message)
{
    if (!lock_state(state)) return;
    state->value.kvm_phase = phase;
    if (message != NULL) copy_text(state->value.last_message,
                                   sizeof(state->value.last_message), message);
    changed(state);
    xSemaphoreGive(state->mutex);
}

void app_state_commit_switch(app_state_t *state, host_id_t target, bool confirmed)
{
    if (target >= HOST_COUNT || !lock_state(state)) return;
    state->value.display_target = target;
    state->value.usb_owner = target;
    state->value.display_confidence = confirmed ? CONFIDENCE_CONFIRMED : CONFIDENCE_ESTIMATED;
    state->value.usb_confidence = CONFIDENCE_ESTIMATED;
    changed(state);
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
    changed(state);
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
    changed(state);
    xSemaphoreGive(state->mutex);
}

void app_state_set_command_failed(app_state_t *state, const char *request_id,
                                  command_status_t status, const char *error)
{
    if (status != COMMAND_TIMEOUT && status != COMMAND_DISCONNECTED &&
        status != COMMAND_SEND_FAILED && status != COMMAND_REJECTED) return;
    if (!lock_state(state)) return;
    state->value.command_status = status;
    state->value.command_success = false;
    state->value.command_write_succeeded = false;
    state->value.command_confirmed = false;
    copy_text(state->value.command_request_id, sizeof(state->value.command_request_id), request_id);
    copy_text(state->value.command_error, sizeof(state->value.command_error), error);
    changed(state);
    xSemaphoreGive(state->mutex);
}
