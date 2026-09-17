#include "websocket_server.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "app_config.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "protocol.h"

#define PENDING_COMMAND_COUNT 4
#define COMMAND_JSON_CAPACITY 512
#define LOG_RATE_LIMIT_MS 5000

typedef struct {
    int fd;
    uint32_t telemetry_count;
    protocol_session_t protocol;
} host_connection_t;

typedef enum {
    PENDING_SWITCH_DISPLAY = 0,
    PENDING_DISPLAY_POWER,
} pending_kind_t;

typedef struct {
    bool used;
    int fd;
    host_id_t host;
    pending_kind_t kind;
    display_power_action_t power_action;
    int expected_input;
    int64_t deadline_ms;
    char request_id[PROTOCOL_REQUEST_ID_MAX + 1];
} pending_command_t;

typedef struct {
    httpd_handle_t server;
    int fd;
    char request_id[PROTOCOL_REQUEST_ID_MAX + 1];
    char payload[COMMAND_JSON_CAPACITY];
} async_send_t;

static const char *TAG = "websocket";
static app_state_t *s_state;
static SemaphoreHandle_t s_mutex;
static httpd_handle_t s_server;
static host_connection_t s_hosts[HOST_COUNT];
static pending_command_t s_pending[PENDING_COMMAND_COUNT];
static QueueHandle_t s_power_results;
static uint32_t s_request_counter;
static int64_t s_last_protocol_log_ms[PROTOCOL_ERROR_SEQUENCE + 1];

static int64_t monotonic_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static host_id_t platform_to_host(protocol_platform_t platform)
{
    return platform == PROTOCOL_PLATFORM_MACOS ? HOST_MAC : HOST_WINDOWS;
}

static void reset_connections_locked(void)
{
    memset(s_hosts, 0, sizeof(s_hosts));
    for (int i = 0; i < HOST_COUNT; ++i) s_hosts[i].fd = -1;
    memset(s_pending, 0, sizeof(s_pending));
}

static int find_host_by_fd_locked(int fd)
{
    for (int i = 0; i < HOST_COUNT; ++i) {
        if (s_hosts[i].fd == fd) return i;
    }
    return -1;
}

static void protocol_log(int fd, protocol_status_t status, const char *detail)
{
    const int64_t now = monotonic_ms();
    const int index = status >= PROTOCOL_OK && status <= PROTOCOL_ERROR_SEQUENCE ? status : 0;
    if (s_last_protocol_log_ms[index] == 0 ||
        now - s_last_protocol_log_ms[index] >= LOG_RATE_LIMIT_MS) {
        s_last_protocol_log_ms[index] = now;
        ESP_LOGW(TAG, "Protocol issue fd=%d: %s (%s)", fd,
                 protocol_status_name(status), detail);
    }
}

static int fail_pending_for_fd_locked(int fd, pending_command_t *failed, int capacity)
{
    int count = 0;
    for (int i = 0; i < PENDING_COMMAND_COUNT; ++i) {
        if (s_pending[i].used && s_pending[i].fd == fd) {
            if (failed != NULL && count < capacity) failed[count++] = s_pending[i];
            s_pending[i].used = false;
        }
    }
    return count;
}

static bool clear_pending_locked(int fd, const char *request_id, pending_command_t *removed)
{
    for (int i = 0; i < PENDING_COMMAND_COUNT; ++i) {
        if (s_pending[i].used && s_pending[i].fd == fd &&
            strcmp(s_pending[i].request_id, request_id) == 0) {
            if (removed != NULL) *removed = s_pending[i];
            s_pending[i].used = false;
            return true;
        }
    }
    return false;
}

static void publish_power_result(const pending_command_t *pending, bool success,
                                 const char *error)
{
    if (pending == NULL || pending->kind != PENDING_DISPLAY_POWER ||
        s_power_results == NULL) return;
    display_power_result_event_t event = {
        .host = pending->host,
        .action = pending->power_action,
        .success = success,
    };
    snprintf(event.request_id, sizeof(event.request_id), "%s", pending->request_id);
    snprintf(event.error, sizeof(event.error), "%s", error != NULL ? error : "");
    (void)xQueueSend(s_power_results, &event, 0);
}

static void publish_pending_failure(const pending_command_t *pending,
                                    command_status_t status, const char *error)
{
    if (pending == NULL) return;
    if (pending->kind == PENDING_DISPLAY_POWER) {
        publish_power_result(pending, false, error);
    } else {
        app_state_set_command_failed(s_state, pending->request_id, status, error);
    }
}

static esp_err_t socket_open(httpd_handle_t server, int fd)
{
    (void)server;
    ESP_LOGI(TAG, "Socket connected: fd=%d", fd);
    return ESP_OK;
}

static void socket_close(httpd_handle_t server, int fd)
{
    (void)server;
    int disconnected_host = -1;
    pending_command_t failed[PENDING_COMMAND_COUNT] = {0};
    int failed_count = 0;
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        disconnected_host = find_host_by_fd_locked(fd);
        if (disconnected_host >= 0) {
            s_hosts[disconnected_host].fd = -1;
            memset(&s_hosts[disconnected_host].protocol, 0,
                   sizeof(s_hosts[disconnected_host].protocol));
        }
        failed_count = fail_pending_for_fd_locked(fd, failed, PENDING_COMMAND_COUNT);
        xSemaphoreGive(s_mutex);
    }
    close(fd);
    if (disconnected_host >= 0) {
        app_state_set_host_offline(s_state, (host_id_t)disconnected_host, "disconnected");
        app_state_set_last_message(s_state, "HOST DISCONNECTED");
    }
    for (int i = 0; i < failed_count; ++i) {
        publish_pending_failure(&failed[i], COMMAND_DISCONNECTED, "connection_closed");
    }
}

static esp_err_t accept_hello(int fd, const protocol_message_t *message)
{
    protocol_session_t session = {0};
    char error[PROTOCOL_ERROR_MAX] = {0};
    if (protocol_session_accept(&session, message, error, sizeof(error)) != PROTOCOL_OK) {
        return ESP_FAIL;
    }
    const host_id_t host = platform_to_host(message->data.hello.platform);
    int replaced_fd = -1;
    pending_command_t replaced[PENDING_COMMAND_COUNT] = {0};
    int replaced_count = 0;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_FAIL;
    const int old_slot = find_host_by_fd_locked(fd);
    if (old_slot >= 0 && old_slot != host) s_hosts[old_slot].fd = -1;
    if (s_hosts[host].fd >= 0 && s_hosts[host].fd != fd) {
        replaced_fd = s_hosts[host].fd;
        replaced_count = fail_pending_for_fd_locked(replaced_fd, replaced,
                                                    PENDING_COMMAND_COUNT);
    }
    s_hosts[host].fd = fd;
    s_hosts[host].telemetry_count = 0;
    s_hosts[host].protocol = session;
    xSemaphoreGive(s_mutex);
    app_state_set_host_online(s_state, host, message->data.hello.device_id, monotonic_ms());
    app_state_set_last_message(s_state, host == HOST_MAC ? "MAC CONNECTED" : "WINDOWS CONNECTED");
    ESP_LOGI(TAG, "%s hello accepted", host == HOST_MAC ? "macOS" : "Windows");
    if (replaced_fd >= 0 && s_server != NULL) httpd_sess_trigger_close(s_server, replaced_fd);
    for (int i = 0; i < replaced_count; ++i) {
        publish_pending_failure(&replaced[i], COMMAND_DISCONNECTED, "connection_replaced");
    }
    return ESP_OK;
}

static esp_err_t accept_telemetry(int fd, const protocol_message_t *message)
{
    char error[PROTOCOL_ERROR_MAX] = {0};
    int host = -1;
    uint32_t count = 0;
    protocol_status_t status = PROTOCOL_ERROR_HELLO_REQUIRED;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_FAIL;
    host = find_host_by_fd_locked(fd);
    if (host >= 0) {
        status = protocol_session_accept(&s_hosts[host].protocol, message, error, sizeof(error));
        if (status == PROTOCOL_OK) count = ++s_hosts[host].telemetry_count;
    } else {
        snprintf(error, sizeof(error), "hello_required");
    }
    xSemaphoreGive(s_mutex);
    if (status != PROTOCOL_OK) {
        protocol_log(fd, status, error);
        return ESP_OK;
    }
    const protocol_telemetry_t *telemetry = &message->data.telemetry;
    host_telemetry_update_t update = {
        .temperature_valid = telemetry->cpu_temperature.valid,
        .cpu_temperature_c = (float)telemetry->cpu_temperature.value,
        .memory_valid = telemetry->memory_usage.valid,
        .memory_percent = (float)telemetry->memory_usage.value,
        .temperature_sampled_at_ms = telemetry->cpu_temperature.sampled_at_milliseconds,
        .memory_sampled_at_ms = telemetry->memory_usage.sampled_at_milliseconds,
        .snapshot_timestamp_ms = telemetry->timestamp_milliseconds,
        .received_monotonic_ms = monotonic_ms(),
        .sequence = telemetry->sequence,
    };
    snprintf(update.temperature_error, sizeof(update.temperature_error), "%s",
             telemetry->cpu_temperature.error);
    snprintf(update.memory_error, sizeof(update.memory_error), "%s",
             telemetry->memory_usage.error);
    app_state_commit_telemetry(s_state, (host_id_t)host, &update);
    if ((count % 30) == 1) {
        ESP_LOGI(TAG, "%s telemetry seq=%" PRIu64,
                 host == HOST_MAC ? "macOS" : "Windows", telemetry->sequence);
    }
    return ESP_OK;
}

static esp_err_t accept_dashboard(int fd, const protocol_message_t *message)
{
    char error[PROTOCOL_ERROR_MAX] = {0};
    protocol_status_t status = PROTOCOL_ERROR_HELLO_REQUIRED;
    int host = -1;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_FAIL;
    host = find_host_by_fd_locked(fd);
    if (host >= 0) {
        status = protocol_session_accept(&s_hosts[host].protocol, message, error, sizeof(error));
    } else {
        snprintf(error, sizeof(error), "hello_required");
    }
    xSemaphoreGive(s_mutex);
    if (status != PROTOCOL_OK) {
        protocol_log(fd, status, error);
        return ESP_OK;
    }
    const protocol_dashboard_t *dashboard = &message->data.dashboard;
    dashboard_update_t update = {
        .sequence = dashboard->sequence,
        .received_monotonic_ms = monotonic_ms(),
        .weather = dashboard->weather,
        .markets = dashboard->markets,
    };
    snprintf(update.error, sizeof(update.error), "%s", dashboard->error);
    const bool selected = app_state_commit_dashboard(s_state, (host_id_t)host, &update);
    ESP_LOGI(TAG, "%s dashboard seq=%" PRIu64 " %s",
             host == HOST_MAC ? "macOS" : "Windows", dashboard->sequence,
             selected ? "selected" : "standby");
    return ESP_OK;
}

static esp_err_t accept_command_result(int fd, const protocol_message_t *message)
{
    char error[PROTOCOL_ERROR_MAX] = {0};
    protocol_status_t status = PROTOCOL_ERROR_HELLO_REQUIRED;
    bool matched = false;
    int host = -1;
    pending_command_t pending = {0};
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_FAIL;
    host = find_host_by_fd_locked(fd);
    if (host >= 0) {
        status = protocol_session_accept(&s_hosts[host].protocol, message, error, sizeof(error));
        if (status == PROTOCOL_OK) {
            for (int i = 0; i < PENDING_COMMAND_COUNT; ++i) {
                if (s_pending[i].used && s_pending[i].fd == fd &&
                    strcmp(s_pending[i].request_id,
                           message->data.command_result.request_id) == 0) {
                    matched = true;
                    pending = s_pending[i];
                    s_pending[i].used = false;
                    break;
                }
            }
        }
    }
    xSemaphoreGive(s_mutex);
    if (status != PROTOCOL_OK) {
        protocol_log(fd, status, error);
        return ESP_OK;
    }
    if (!matched) {
        protocol_log(fd, PROTOCOL_ERROR_SCHEMA, "unknown_or_duplicate_request_id");
        return ESP_OK;
    }
    const protocol_command_result_t *result = &message->data.command_result;
    if (pending.kind == PENDING_DISPLAY_POWER) {
        const char *expected_action = pending.power_action == DISPLAY_POWER_SLEEP
            ? "display_sleep" : "display_wake";
        if (!result->display_power_result || strcmp(result->action, expected_action) != 0) {
            protocol_log(fd, PROTOCOL_ERROR_SCHEMA, "display_power_action_mismatch");
            publish_power_result(&pending, false, "display_power_result_mismatch");
            return ESP_OK;
        }
        publish_power_result(&pending, result->success, result->error);
        ESP_LOGI(TAG, "%s %s result: %s", host == HOST_MAC ? "macOS" : "Windows",
                 expected_action, result->success ? "success" : result->error);
        return ESP_OK;
    }
    if (result->display_power_result || result->requested_input != pending.expected_input) {
        protocol_log(fd, PROTOCOL_ERROR_SCHEMA, "command_result_target_mismatch");
        app_state_set_command_failed(s_state, result->request_id, COMMAND_REJECTED,
                                     "requested_input_mismatch");
        app_state_set_last_message(s_state, "DDC RESULT TARGET REJECTED");
        return ESP_OK;
    }
    app_state_set_command_result(s_state, result->request_id, result->success,
                                 result->write_succeeded, result->confirmed, result->error);
    app_state_set_last_message(s_state,
        result->confirmed ? "DISPLAY SWITCH CONFIRMED" :
        (result->write_succeeded ? "DDC WRITTEN UNCONFIRMED" : "DISPLAY SWITCH FAILED"));
    return ESP_OK;
}

static esp_err_t websocket_handler(httpd_req_t *request)
{
    httpd_ws_frame_t frame = {0};
    esp_err_t result = httpd_ws_recv_frame(request, &frame, 0);
    if (result != ESP_OK) return result;
    const int fd = httpd_req_to_sockfd(request);
    if (frame.len > PROTOCOL_MAX_MESSAGE_BYTES) {
        protocol_log(fd, PROTOCOL_ERROR_TOO_LONG, "message_too_long");
        return ESP_FAIL;
    }
    if (frame.type == HTTPD_WS_TYPE_PONG) {
        uint8_t payload[126];
        if (frame.len > sizeof(payload)) return ESP_FAIL;
        if (frame.len > 0) {
            frame.payload = payload;
            return httpd_ws_recv_frame(request, &frame, frame.len);
        }
        return ESP_OK;
    }
    if (frame.len == 0) {
        protocol_log(fd, PROTOCOL_ERROR_SCHEMA, "empty_message");
        return ESP_OK;
    }
    if (frame.type != HTTPD_WS_TYPE_TEXT || frame.fragmented || !frame.final) {
        protocol_log(fd, PROTOCOL_ERROR_SCHEMA, "unfragmented_text_required");
        return ESP_FAIL;
    }
    char *payload = malloc(frame.len + 1);
    if (payload == NULL) return ESP_ERR_NO_MEM;
    frame.payload = (uint8_t *)payload;
    result = httpd_ws_recv_frame(request, &frame, frame.len);
    if (result != ESP_OK) { free(payload); return result; }
    payload[frame.len] = '\0';
    protocol_message_t message;
    char error[PROTOCOL_ERROR_MAX] = {0};
    const protocol_status_t status = protocol_parse_message(payload, frame.len, &message,
                                                             error, sizeof(error));
    free(payload);
    if (status != PROTOCOL_OK) {
        protocol_log(fd, status, error);
        return ESP_OK;
    }
    if (message.type == PROTOCOL_MESSAGE_HELLO) return accept_hello(fd, &message);
    if (message.type == PROTOCOL_MESSAGE_TELEMETRY) return accept_telemetry(fd, &message);
    if (message.type == PROTOCOL_MESSAGE_DASHBOARD) return accept_dashboard(fd, &message);
    return accept_command_result(fd, &message);
}

static void async_send_work(void *argument)
{
    async_send_t *send = argument;
    httpd_ws_frame_t frame = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)send->payload,
        .len = strlen(send->payload),
    };
    const esp_err_t result = httpd_ws_send_frame_async(send->server, send->fd, &frame);
    if (result != ESP_OK) {
        pending_command_t failed = {0};
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            clear_pending_locked(send->fd, send->request_id, &failed);
            xSemaphoreGive(s_mutex);
        }
        publish_pending_failure(&failed, COMMAND_SEND_FAILED, "websocket_send_failed");
    }
    free(send);
}

esp_err_t websocket_server_init(app_state_t *state)
{
    if (state == NULL) return ESP_ERR_INVALID_ARG;
    s_state = state;
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) return ESP_ERR_NO_MEM;
    s_power_results = xQueueCreate(8, sizeof(display_power_result_event_t));
    if (s_power_results == NULL) return ESP_ERR_NO_MEM;
    reset_connections_locked();
    return ESP_OK;
}

esp_err_t websocket_server_start(void)
{
    if (s_mutex == NULL) return ESP_ERR_INVALID_STATE;
    if (s_server != NULL) return ESP_OK;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WEBSOCKET_SERVER_PORT;
    config.max_open_sockets = 4;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    config.open_fn = socket_open;
    config.close_fn = socket_close;
    esp_err_t result = httpd_start(&s_server, &config);
    if (result != ESP_OK) { s_server = NULL; return result; }
    const httpd_uri_t uri = {
        .uri = WEBSOCKET_SERVER_PATH,
        .method = HTTP_GET,
        .handler = websocket_handler,
        .is_websocket = true,
        .handle_ws_control_frames = false,
    };
    result = httpd_register_uri_handler(s_server, &uri);
    if (result != ESP_OK) {
        httpd_stop(s_server);
        s_server = NULL;
        return result;
    }
    ESP_LOGI(TAG, "Listening on ws://<device-ip>:%d%s", WEBSOCKET_SERVER_PORT,
             WEBSOCKET_SERVER_PATH);
    return ESP_OK;
}

void websocket_server_stop(void)
{
    httpd_handle_t server = s_server;
    s_server = NULL;
    if (server != NULL) httpd_stop(server);
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        reset_connections_locked();
        xSemaphoreGive(s_mutex);
    }
}

void websocket_server_maintenance(int64_t now_ms)
{
    app_state_mark_dashboard_stale(s_state, now_ms);
    pending_command_t timed_out[PENDING_COMMAND_COUNT] = {0};
    int timed_out_count = 0;
    if (s_mutex == NULL || xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return;
    for (int i = 0; i < PENDING_COMMAND_COUNT; ++i) {
        if (s_pending[i].used && now_ms >= s_pending[i].deadline_ms) {
            timed_out[timed_out_count++] = s_pending[i];
            s_pending[i].used = false;
        }
    }
    xSemaphoreGive(s_mutex);
    for (int i = 0; i < timed_out_count; ++i) {
        publish_pending_failure(&timed_out[i], COMMAND_TIMEOUT, "command_timeout");
        if (timed_out[i].kind == PENDING_SWITCH_DISPLAY) {
            app_state_set_last_message(s_state, "DISPLAY COMMAND TIMED OUT");
        }
    }
}

esp_err_t websocket_server_send_switch_display(host_id_t command_host,
                                               const char *target,
                                               char *request_id,
                                               size_t request_id_capacity)
{
    if (command_host >= HOST_COUNT || target == NULL || request_id == NULL ||
        request_id_capacity == 0 ||
        (strcmp(target, "mac") != 0 && strcmp(target, "windows") != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    async_send_t *send = calloc(1, sizeof(*send));
    if (send == NULL) return ESP_ERR_NO_MEM;
    int pending_index = -1;
    if (s_mutex == NULL || xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        free(send);
        return ESP_ERR_INVALID_STATE;
    }
    host_connection_t *connection = &s_hosts[command_host];
    if (s_server == NULL || connection->fd < 0 || !connection->protocol.hello_received) {
        xSemaphoreGive(s_mutex);
        free(send);
        return ESP_ERR_INVALID_STATE;
    }
    for (int i = 0; i < PENDING_COMMAND_COUNT; ++i) {
        if (!s_pending[i].used) { pending_index = i; break; }
    }
    if (pending_index < 0) {
        xSemaphoreGive(s_mutex);
        free(send);
        return ESP_ERR_NO_MEM;
    }
    const uint32_t sequence = ++s_request_counter;
    char generated[PROTOCOL_REQUEST_ID_MAX + 1];
    snprintf(generated, sizeof(generated), "esp32-%08" PRIx32 "-%08" PRIx32,
             (uint32_t)monotonic_ms(), sequence);
    if (strlen(generated) >= request_id_capacity ||
        !protocol_build_switch_command(send->payload, sizeof(send->payload), generated,
                                       target, connection->protocol.device_id)) {
        xSemaphoreGive(s_mutex);
        free(send);
        return ESP_ERR_INVALID_SIZE;
    }
    send->server = s_server;
    send->fd = connection->fd;
    snprintf(send->request_id, sizeof(send->request_id), "%s", generated);
    pending_command_t *pending = &s_pending[pending_index];
    pending->used = true;
    pending->fd = connection->fd;
    pending->host = command_host;
    pending->kind = PENDING_SWITCH_DISPLAY;
    pending->expected_input = strcmp(target, "mac") == 0 ? APP_INPUT_HDMI2 : APP_INPUT_DP;
    pending->deadline_ms = monotonic_ms() + APP_COMMAND_TIMEOUT_MS;
    snprintf(pending->request_id, sizeof(pending->request_id), "%s", generated);
    snprintf(request_id, request_id_capacity, "%s", generated);
    xSemaphoreGive(s_mutex);
    app_state_set_command_pending(s_state, generated);
    const esp_err_t result = httpd_queue_work(send->server, async_send_work, send);
    if (result != ESP_OK) {
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            clear_pending_locked(send->fd, send->request_id, NULL);
            xSemaphoreGive(s_mutex);
        }
        app_state_set_command_failed(s_state, send->request_id, COMMAND_SEND_FAILED,
                                     "queue_send_failed");
        free(send);
    }
    return result;
}

bool websocket_server_host_supports_display_power(host_id_t host,
                                                  display_power_action_t action)
{
    if (host >= HOST_COUNT || s_mutex == NULL) return false;
    bool supported = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        const host_connection_t *connection = &s_hosts[host];
        if (connection->fd >= 0 && connection->protocol.hello_received) {
            supported = action == DISPLAY_POWER_SLEEP
                ? connection->protocol.supports_display_sleep
                : connection->protocol.supports_display_wake;
        }
        xSemaphoreGive(s_mutex);
    }
    return supported;
}

esp_err_t websocket_server_send_display_power(host_id_t host,
                                              display_power_action_t action,
                                              char *request_id,
                                              size_t request_id_capacity)
{
    if (host >= HOST_COUNT || action > DISPLAY_POWER_WAKE || request_id == NULL ||
        request_id_capacity == 0) return ESP_ERR_INVALID_ARG;
    async_send_t *send = calloc(1, sizeof(*send));
    if (send == NULL) return ESP_ERR_NO_MEM;
    int pending_index = -1;
    if (s_mutex == NULL || xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        free(send);
        return ESP_ERR_INVALID_STATE;
    }
    host_connection_t *connection = &s_hosts[host];
    const bool supported = action == DISPLAY_POWER_SLEEP
        ? connection->protocol.supports_display_sleep
        : connection->protocol.supports_display_wake;
    if (s_server == NULL || connection->fd < 0 ||
        !connection->protocol.hello_received || !supported) {
        xSemaphoreGive(s_mutex);
        free(send);
        return ESP_ERR_NOT_SUPPORTED;
    }
    for (int i = 0; i < PENDING_COMMAND_COUNT; ++i) {
        if (!s_pending[i].used) { pending_index = i; break; }
    }
    if (pending_index < 0) {
        xSemaphoreGive(s_mutex);
        free(send);
        return ESP_ERR_NO_MEM;
    }
    const uint32_t sequence = ++s_request_counter;
    char generated[PROTOCOL_REQUEST_ID_MAX + 1];
    const char *action_name = action == DISPLAY_POWER_SLEEP
        ? "display_sleep" : "display_wake";
    snprintf(generated, sizeof(generated), "esp32-%s-%08" PRIx32 "-%08" PRIx32,
             action == DISPLAY_POWER_SLEEP ? "sleep" : "wake",
             (uint32_t)monotonic_ms(), sequence);
    if (strlen(generated) >= request_id_capacity ||
        !protocol_build_display_power_command(send->payload, sizeof(send->payload),
                                              generated, action_name,
                                              connection->protocol.device_id)) {
        xSemaphoreGive(s_mutex);
        free(send);
        return ESP_ERR_INVALID_SIZE;
    }
    send->server = s_server;
    send->fd = connection->fd;
    snprintf(send->request_id, sizeof(send->request_id), "%s", generated);
    pending_command_t *pending = &s_pending[pending_index];
    pending->used = true;
    pending->fd = connection->fd;
    pending->host = host;
    pending->kind = PENDING_DISPLAY_POWER;
    pending->power_action = action;
    pending->deadline_ms = monotonic_ms() + APP_COMMAND_TIMEOUT_MS;
    snprintf(pending->request_id, sizeof(pending->request_id), "%s", generated);
    snprintf(request_id, request_id_capacity, "%s", generated);
    xSemaphoreGive(s_mutex);

    const esp_err_t result = httpd_queue_work(send->server, async_send_work, send);
    if (result != ESP_OK) {
        pending_command_t failed = {0};
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            clear_pending_locked(send->fd, send->request_id, &failed);
            xSemaphoreGive(s_mutex);
        }
        publish_pending_failure(&failed, COMMAND_SEND_FAILED, "queue_send_failed");
        free(send);
    }
    return result;
}

bool websocket_server_take_display_power_result(display_power_result_event_t *event)
{
    return event != NULL && s_power_results != NULL &&
           xQueueReceive(s_power_results, event, 0) == pdTRUE;
}
