#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_state.h"

#define PROTOCOL_VERSION 1
#define PROTOCOL_MAX_MESSAGE_BYTES 2048
#define PROTOCOL_DEVICE_ID_MAX 128
#define PROTOCOL_REQUEST_ID_MAX 128
#define PROTOCOL_ERROR_MAX 64

typedef enum {
    PROTOCOL_MESSAGE_HELLO = 0,
    PROTOCOL_MESSAGE_TELEMETRY,
    PROTOCOL_MESSAGE_DASHBOARD,
    PROTOCOL_MESSAGE_COMMAND_RESULT,
} protocol_message_type_t;

typedef enum {
    PROTOCOL_PLATFORM_MACOS = 0,
    PROTOCOL_PLATFORM_WINDOWS,
} protocol_platform_t;

typedef enum {
    PROTOCOL_OK = 0,
    PROTOCOL_ERROR_TOO_LONG,
    PROTOCOL_ERROR_INVALID_UTF8,
    PROTOCOL_ERROR_INVALID_JSON,
    PROTOCOL_ERROR_SCHEMA,
    PROTOCOL_ERROR_UNSUPPORTED_VERSION,
    PROTOCOL_ERROR_UNSUPPORTED_PLATFORM,
    PROTOCOL_ERROR_HELLO_REQUIRED,
    PROTOCOL_ERROR_IDENTITY_MISMATCH,
    PROTOCOL_ERROR_SEQUENCE,
} protocol_status_t;

typedef struct {
    bool valid;
    double value;
    int64_t sampled_at_milliseconds;
    char error[PROTOCOL_ERROR_MAX];
} protocol_metric_t;

typedef struct {
    protocol_platform_t platform;
    char device_id[PROTOCOL_DEVICE_ID_MAX + 1];
    char app[33];
    bool supports_display_sleep;
    bool supports_display_wake;
} protocol_hello_t;

typedef struct {
    protocol_platform_t platform;
    char device_id[PROTOCOL_DEVICE_ID_MAX + 1];
    uint64_t sequence;
    int64_t timestamp_milliseconds;
    protocol_metric_t cpu_temperature;
    protocol_metric_t memory_usage;
} protocol_telemetry_t;

typedef struct {
    protocol_platform_t platform;
    char device_id[PROTOCOL_DEVICE_ID_MAX + 1];
    uint64_t sequence;
    int64_t timestamp_milliseconds;
    weather_snapshot_t weather;
    market_snapshot_t markets;
    char error[PROTOCOL_ERROR_MAX];
} protocol_dashboard_t;

typedef struct {
    char device_id[PROTOCOL_DEVICE_ID_MAX + 1];
    char request_id[PROTOCOL_REQUEST_ID_MAX + 1];
    bool display_power_result;
    char action[24];
    bool success;
    char display_power_state[24];
    bool write_succeeded;
    bool confirmed;
    int requested_input;
    bool read_back_input_valid;
    int read_back_input;
    char error[PROTOCOL_ERROR_MAX];
} protocol_command_result_t;

typedef struct {
    protocol_message_type_t type;
    union {
        protocol_hello_t hello;
        protocol_telemetry_t telemetry;
        protocol_dashboard_t dashboard;
        protocol_command_result_t command_result;
    } data;
} protocol_message_t;

typedef struct {
    bool hello_received;
    protocol_platform_t platform;
    char device_id[PROTOCOL_DEVICE_ID_MAX + 1];
    bool supports_display_sleep;
    bool supports_display_wake;
    bool has_sequence;
    uint64_t last_sequence;
    bool has_dashboard_sequence;
    uint64_t last_dashboard_sequence;
} protocol_session_t;

protocol_status_t protocol_parse_message(const char *payload, size_t length,
                                         protocol_message_t *message,
                                         char *error, size_t error_capacity);
protocol_status_t protocol_session_accept(protocol_session_t *session,
                                          const protocol_message_t *message,
                                          char *error, size_t error_capacity);
bool protocol_build_switch_command(char *output, size_t output_capacity,
                                   const char *request_id, const char *target,
                                   const char *target_device_id);
bool protocol_build_display_power_command(char *output, size_t output_capacity,
                                          const char *request_id, const char *action,
                                          const char *target_device_id);
bool protocol_is_valid_utf8(const uint8_t *data, size_t length);
const char *protocol_status_name(protocol_status_t status);
