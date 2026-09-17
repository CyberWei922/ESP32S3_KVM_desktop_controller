#pragma once

#include <stddef.h>

#include "app_state.h"
#include "esp_err.h"

#define WEBSOCKET_SERVER_PORT 81
#define WEBSOCKET_SERVER_PATH "/statsforkvm"

esp_err_t websocket_server_init(app_state_t *state);
esp_err_t websocket_server_start(void);
void websocket_server_stop(void);
void websocket_server_maintenance(int64_t now_monotonic_ms);

// Manual command API for the future button layer. Nothing calls this automatically.
esp_err_t websocket_server_send_switch_display(host_id_t destination_host,
                                               const char *target,
                                               char *request_id,
                                               size_t request_id_capacity);
