#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define APP_STATE_DEVICE_ID_MAX 128
#define APP_STATE_ERROR_MAX 64
#define APP_STATE_MESSAGE_MAX 96
#define APP_STATE_CITY_MAX 32
#define APP_STATE_PROVIDER_MAX 16
#define APP_WEATHER_DAILY_COUNT 3
#define APP_WEATHER_HOURLY_COUNT 5

typedef enum {
    HOST_MAC = 0,
    HOST_WINDOWS,
    HOST_COUNT,
} host_id_t;

typedef enum {
    CONFIDENCE_UNKNOWN = 0,
    CONFIDENCE_ESTIMATED,
    CONFIDENCE_CONFIRMED,
} confidence_t;

typedef enum {
    THERMAL_MANUAL = 0,
    THERMAL_STANDARD,
    THERMAL_SILENT,
    THERMAL_TURBO,
    THERMAL_MODE_COUNT,
} thermal_mode_t;

typedef enum {
    COOLER_UNKNOWN = 0,
    COOLER_INITIALIZING,
    COOLER_OFF,
    COOLER_LEVEL_1,
    COOLER_LEVEL_2,
    COOLER_LEVEL_3,
    COOLER_LEVEL_4,
    COOLER_ERROR,
} cooler_state_t;

typedef enum {
    KVM_IDLE = 0,
    KVM_DISABLED_UNCONFIRMED_TIMING,
    KVM_SENDING_DDC,
    KVM_WAITING_DDC_RESULT,
    KVM_WAITING_DISPLAY,
    KVM_PULSING_USB,
    KVM_COOLING,
    KVM_ERROR,
} kvm_phase_t;

typedef enum {
    COMMAND_IDLE = 0,
    COMMAND_PENDING,
    COMMAND_RESULT,
    COMMAND_TIMEOUT,
    COMMAND_DISCONNECTED,
    COMMAND_SEND_FAILED,
    COMMAND_REJECTED,
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
    int64_t last_update_monotonic_ms;
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
    int16_t temperature_c;
    int16_t high_c;
    int16_t low_c;
    uint16_t weather_code;
    bool is_day;
} weather_current_t;

typedef struct {
    char day[4];
    int16_t high_c;
    int16_t low_c;
    uint16_t weather_code;
} weather_daily_t;

typedef struct {
    uint8_t hour;
    int16_t temperature_c;
    uint16_t weather_code;
    bool is_day;
} weather_hourly_t;

typedef struct {
    bool valid;
    char provider[APP_STATE_PROVIDER_MAX];
    char city_code[APP_STATE_CITY_MAX];
    char city_name[APP_STATE_CITY_MAX];
    int64_t updated_at_ms;
    weather_current_t current;
    weather_daily_t daily[APP_WEATHER_DAILY_COUNT];
    weather_hourly_t hourly[APP_WEATHER_HOURLY_COUNT];
} weather_snapshot_t;

typedef struct {
    double price;
    float change_percent;
} market_quote_t;

typedef struct {
    bool valid;
    int64_t updated_at_ms;
    market_quote_t btc_usdt;
    market_quote_t doge_usdt;
    market_quote_t usd_cny;
} market_snapshot_t;

typedef struct {
    uint64_t sequence;
    int64_t received_monotonic_ms;
    weather_snapshot_t weather;
    market_snapshot_t markets;
    char error[APP_STATE_ERROR_MAX];
} dashboard_update_t;

typedef struct {
    host_status_t hosts[HOST_COUNT];
    bool wifi_configured;
    bool wifi_connected;
    char ip_address[16];
    host_id_t display_target;
    confidence_t display_confidence;
    host_id_t usb_owner;
    confidence_t usb_confidence;
    thermal_mode_t thermal_mode;
    bool cooler_control_enabled;
    cooler_state_t cooler_state;
    uint8_t cooler_level;
    int16_t thermal_targets_c[THERMAL_MODE_COUNT];
    bool backlight_enabled;
    uint8_t data_window_index;
    bool dashboard_valid;
    bool dashboard_stale;
    host_id_t dashboard_source;
    int64_t dashboard_received_monotonic_ms;
    uint64_t dashboard_sequence;
    weather_snapshot_t weather;
    market_snapshot_t markets;
    char dashboard_error[APP_STATE_ERROR_MAX];
    int64_t windows_dashboard_recovery_since_ms;
    int64_t windows_dashboard_candidate_last_ms;
    uint64_t dashboard_revision;
    kvm_phase_t kvm_phase;
    command_status_t command_status;
    bool command_success;
    bool command_write_succeeded;
    bool command_confirmed;
    char command_request_id[APP_STATE_DEVICE_ID_MAX + 1];
    char command_error[APP_STATE_ERROR_MAX];
    char last_message[APP_STATE_MESSAGE_MAX];
    uint64_t revision;
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
                               int64_t now_ms);
void app_state_set_host_offline(app_state_t *state, host_id_t host, const char *reason);
void app_state_commit_telemetry(app_state_t *state, host_id_t host,
                                const host_telemetry_update_t *update);
uint32_t app_state_mark_stale_hosts(app_state_t *state, int64_t now_ms);
void app_state_set_last_message(app_state_t *state, const char *message);
void app_state_set_backlight(app_state_t *state, bool enabled);
void app_state_next_data_window(app_state_t *state);
void app_state_previous_data_window(app_state_t *state);
bool app_state_commit_dashboard(app_state_t *state, host_id_t host,
                                const dashboard_update_t *update);
void app_state_mark_dashboard_stale(app_state_t *state, int64_t now_ms);
void app_state_next_thermal_mode(app_state_t *state);
void app_state_set_thermal_mode(app_state_t *state, thermal_mode_t mode);
int app_state_adjust_thermal_target(app_state_t *state, int delta_c);
void app_state_set_thermal_target(app_state_t *state, thermal_mode_t mode, int target_c);
void app_state_set_cooler_status(app_state_t *state, cooler_state_t cooler_state,
                                 uint8_t level, bool enabled, const char *message);
void app_state_commit_usb_correction(app_state_t *state);
void app_state_set_kvm_phase(app_state_t *state, kvm_phase_t phase, const char *message);
void app_state_commit_switch(app_state_t *state, host_id_t target, bool confirmed);
void app_state_set_command_pending(app_state_t *state, const char *request_id);
void app_state_set_command_result(app_state_t *state, const char *request_id, bool success,
                                  bool write_succeeded, bool confirmed, const char *error);
void app_state_set_command_failed(app_state_t *state, const char *request_id,
                                  command_status_t status, const char *error);
