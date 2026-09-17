#pragma once

#include <stdbool.h>
#include <stdint.h>

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

typedef struct {
    bool online;
    bool temperature_valid;
    float cpu_temperature_c;
    bool memory_valid;
    uint64_t memory_used_bytes;
    uint64_t memory_total_bytes;
    float memory_percent;
    int64_t sampled_at_ms;
    int64_t received_at_ms;
    char error[64];
} host_status_t;

typedef struct {
    host_status_t hosts[HOST_COUNT];
    bool wifi_connected;
    char ip_address[16];
    host_id_t active_host;
    confidence_t display_input_confidence;
    confidence_t usb_owner_confidence;
    thermal_mode_t thermal_mode;
    confidence_t cooler_level_confidence;
    uint8_t cooler_level;
    char last_message[96];
} app_state_t;

void app_state_init(app_state_t *state);
void app_state_load_demo_data(app_state_t *state);
