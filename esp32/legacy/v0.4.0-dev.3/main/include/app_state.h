#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define APP_STATE_DEVICE_ID_MAX 128
#define APP_STATE_ERROR_MAX 64
#define APP_STATE_STALE_TIMEOUT_MS 5000

typedef enum {
    HOST_MAC = 0,
    HOST_WINDOWS,
    HOST_COUNT,
} host_id_t;

typedef enum {
    THERMAL_MODE_QUIET = 0,
    THERMAL_MODE_NORMAL,
    THERMAL_MODE_TURBO,
} thermal_mode_t;

typedef enum {
    STATE_UNKNOWN = 0,
    STATE_ESTIMATED,
    STATE_CONFIRMED,
} confidence_t;

typedef enum {
    COMMAND_IDLE = 0,
    COMMAND_PENDING,
    COMMAND_RESULT,
    COMMAND_TIMEOUT,
    COMMAND_DISCONNECTED,
    COMMAND_SEND_FAILED,
} command_status_t;

typedef struct {
    bool online;
    bool stale;
    bool temperature_valid;
    float cpu_temperature_c;
    bool memory_valid;
    float memory_percent;
    int64_t temperature_sampled_at_ms;
    int64_t memory_sampled_at_ms;
    int64_t snapshot_timestamp_ms;
    int64_t last_telemetry_monotonic_ms;
    uint64_t sequence;
    char device_id[APP_STATE_DEVICE_ID_MAX + 1];
    char temperature_error[APP_STATE_ERROR_MAX];
    char memory_error[APP_STATE_ERROR_MAX];
} host_status_t;

typedef struct {
    bool temperature_valid;
    float cpu_temperature_c;
    bool memory_valid;
    float memory_percent;
    int64_t temperature_sampled_at_ms;
    int64_t memory_sampled_at_ms;
    int64_t snapshot_timestamp_ms;
    int64_t received_monotonic_ms;
    uint64_t sequence;
    char temperature_error[APP_STATE_ERROR_MAX];
    char memory_error[APP_STATE_ERROR_MAX];
} host_telemetry_update_t;

typedef struct {
    host_status_t hosts[HOST_COUNT];
    bool wifi_configured;
    bool wifi_connected;
    char ip_address[16];
    host_id_t active_host;
    confidence_t display_input_confidence;
    confidence_t usb_owner_confidence;
    thermal_mode_t thermal_mode;
    confidence_t cooler_level_confidence;
    uint8_t cooler_level;
    command_status_t command_status;
    bool command_success;
    bool command_write_succeeded;
    bool command_confirmed;
    char command_request_id[129];
    char command_error[APP_STATE_ERROR_MAX];
    char last_message[96];
} app_state_snapshot_t;

typedef struct {
    SemaphoreHandle_t mutex;
    app_state_snapshot_t value;
} app_state_t;

esp_err_t app_state_init(app_state_t *state);
bool app_state_get_snapshot(app_state_t *state, app_state_snapshot_t *snapshot);
void app_state_set_wifi_configured(app_state_t *state, bool configured);
void app_state_set_wifi(app_state_t *state, bool connected, const char *ip_address,
                        const char *message);
void app_state_set_host_online(app_state_t *state, host_id_t host, const char *device_id,
                               int64_t connected_monotonic_ms);
void app_state_set_host_offline(app_state_t *state, host_id_t host, const char *reason);
void app_state_commit_telemetry(app_state_t *state, host_id_t host,
                                const host_telemetry_update_t *update);
uint32_t app_state_mark_stale_hosts(app_state_t *state, int64_t now_monotonic_ms);
void app_state_set_last_message(app_state_t *state, const char *message);
void app_state_set_command_pending(app_state_t *state, const char *request_id);
void app_state_set_command_result(app_state_t *state, const char *request_id, bool success,
                                  bool write_succeeded, bool confirmed, const char *error);
void app_state_set_command_failed(app_state_t *state, const char *request_id,
                                  command_status_t status, const char *error);
