#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app_state.h"
#include "esp_err.h"

#define WEBSOCKET_SERVER_PORT 81
#define WEBSOCKET_SERVER_PATH "/statsforkvm"

typedef enum {
    DISPLAY_POWER_SLEEP = 0,
    DISPLAY_POWER_WAKE,
} display_power_action_t;

typedef struct {
    host_id_t host;
    display_power_action_t action;
    bool success;
    char request_id[APP_STATE_DEVICE_ID_MAX + 1];
    char error[APP_STATE_ERROR_MAX];
} display_power_result_event_t;

esp_err_t websocket_server_init(app_state_t *state);
esp_err_t websocket_server_start(void);
void websocket_server_stop(void);
void websocket_server_maintenance(int64_t now_ms);
esp_err_t websocket_server_send_switch_display(host_id_t command_host,
                                               const char *target,
                                               char *request_id,
                                               size_t request_id_capacity);
bool websocket_server_host_supports_display_power(host_id_t host,
                                                  display_power_action_t action);
esp_err_t websocket_server_send_display_power(host_id_t host,
                                              display_power_action_t action,
                                              char *request_id,
                                              size_t request_id_capacity);
bool websocket_server_take_display_power_result(display_power_result_event_t *event);
