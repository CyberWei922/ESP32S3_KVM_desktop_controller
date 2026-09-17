#include "websocket_server.h"

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "protocol.h"

#define PENDING_COMMAND_COUNT 4
#define COMMAND_TIMEOUT_MS 5000
#define COMMAND_JSON_CAPACITY 512
#define LOG_RATE_LIMIT_MS 5000

typedef struct {
    int fd;
    uint32_t generation;
    uint32_t telemetry_count;
    protocol_session_t protocol;
} host_connection_t;

typedef struct {
    bool used;
    int fd;
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
static app_state_t *s_app_state;
static SemaphoreHandle_t s_mutex;
static httpd_handle_t s_server;
static host_connection_t s_hosts[HOST_COUNT];
static pending_command_t s_pending[PENDING_COMMAND_COUNT];
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

static void rate_limited_protocol_log(int fd, protocol_status_t status, const char *detail)
{
    int64_t now = monotonic_ms();
    int index = status >= PROTOCOL_OK && status <= PROTOCOL_ERROR_SEQUENCE ? status : 0;
    if (s_last_protocol_log_ms[index] == 0 ||
        now - s_last_protocol_log_ms[index] >= LOG_RATE_LIMIT_MS) {
        s_last_protocol_log_ms[index] = now;
        ESP_LOGW(TAG, "Protocol issue fd=%d: %s (%s)", fd,
                 protocol_status_name(status), detail);
    }
}

static esp_err_t websocket_open(httpd_handle_t server, int fd)
{
    (void)server;
    ESP_LOGI(TAG, "Client socket connected: fd=%d", fd);
    return ESP_OK;
}

static int find_host_by_fd_locked(int fd)
{
    for (int i = 0; i < HOST_COUNT; ++i) {
        if (s_hosts[i].fd == fd) return i;
    }
    return -1;
}

static void fail_pending_for_fd_locked(int fd, char *last_request, size_t capacity)
{
    for (int i = 0; i < PENDING_COMMAND_COUNT; ++i) {
        if (s_pending[i].used && s_pending[i].fd == fd) {
            if (last_request != NULL) {
                snprintf(last_request, capacity, "%s", s_pending[i].request_id);
            }
            s_pending[i].used = false;
        }
    }
}

static void clear_pending_request_locked(int fd, const char *request_id)
{
    for (int i = 0; i < PENDING_COMMAND_COUNT; ++i) {
        if (s_pending[i].used && s_pending[i].fd == fd &&
            strcmp(s_pending[i].request_id, request_id) == 0) {
            s_pending[i].used = false;
            return;
        }
    }
}

static void websocket_close(httpd_handle_t server, int fd)
{
    (void)server;
    int disconnected_host = -1;
    char failed_request[PROTOCOL_REQUEST_ID_MAX + 1] = {0};
    if (s_mutex != NULL && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        disconnected_host = find_host_by_fd_locked(fd);
        if (disconnected_host >= 0) {
            s_hosts[disconnected_host].fd = -1;
            memset(&s_hosts[disconnected_host].protocol, 0,
                   sizeof(s_hosts[disconnected_host].protocol));
        }
        fail_pending_for_fd_locked(fd, failed_request, sizeof(failed_request));
        xSemaphoreGive(s_mutex);
    }
    // A custom ESP-IDF close callback owns the actual socket close.
    close(fd);
    if (disconnected_host >= 0 && s_app_state != NULL) {
        app_state_set_host_offline(s_app_state, (host_id_t)disconnected_host, "disconnected");
        app_state_set_last_message(s_app_state, "HOST DISCONNECTED");
    }
    if (failed_request[0] != '\0') {
        app_state_set_command_failed(s_app_state, failed_request, COMMAND_DISCONNECTED,
                                     "connection_closed");
    }
}

static esp_err_t accept_hello(int fd, const protocol_message_t *message)
{
    protocol_session_t session = {0};
    char error[PROTOCOL_ERROR_MAX] = {0};
    protocol_status_t status = protocol_session_accept(&session, message, error, sizeof(error));
    if (status != PROTOCOL_OK) return ESP_FAIL;

    host_id_t host = platform_to_host(message->data.hello.platform);
    int replaced_fd = -1;
    char replaced_request[PROTOCOL_REQUEST_ID_MAX + 1] = {0};
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_FAIL;
    int previous_host = find_host_by_fd_locked(fd);
    if (previous_host >= 0 && previous_host != host) s_hosts[previous_host].fd = -1;
    if (s_hosts[host].fd >= 0 && s_hosts[host].fd != fd) {
        replaced_fd = s_hosts[host].fd;
        fail_pending_for_fd_locked(replaced_fd, replaced_request, sizeof(replaced_request));
    }
    s_hosts[host].fd = fd;
    ++s_hosts[host].generation;
    s_hosts[host].telemetry_count = 0;
    s_hosts[host].protocol = session;
    xSemaphoreGive(s_mutex);

    app_state_set_host_online(s_app_state, host, message->data.hello.device_id, monotonic_ms());
    app_state_set_last_message(s_app_state,
                               host == HOST_MAC ? "MAC CONNECTED" : "WINDOWS CONNECTED");
    ESP_LOGI(TAG, "%s hello accepted: %s", host == HOST_MAC ? "macOS" : "Windows",
             message->data.hello.device_id);
    if (replaced_fd >= 0 && s_server != NULL) {
        ESP_LOGI(TAG, "Replacing previous %s connection", host == HOST_MAC ? "macOS" : "Windows");
        httpd_sess_trigger_close(s_server, replaced_fd);
    }
    if (replaced_request[0] != '\0') {
        app_state_set_command_failed(s_app_state, replaced_request, COMMAND_DISCONNECTED,
                                     "connection_replaced");
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
        rate_limited_protocol_log(fd, status, error);
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
    app_state_commit_telemetry(s_app_state, (host_id_t)host, &update);
    if (!telemetry->cpu_temperature.valid || !telemetry->memory_usage.valid) {
        rate_limited_protocol_log(fd, PROTOCOL_ERROR_SCHEMA,
            !telemetry->cpu_temperature.valid ? telemetry->cpu_temperature.error
                                              : telemetry->memory_usage.error);
    }
    if ((count % 30) == 1) {
        ESP_LOGI(TAG, "%s telemetry seq=%" PRIu64 " cpu=%s memory=%s",
                 host == HOST_MAC ? "macOS" : "Windows", telemetry->sequence,
                 telemetry->cpu_temperature.valid ? "valid" : "invalid",
                 telemetry->memory_usage.valid ? "valid" : "invalid");
    }
    return ESP_OK;
}

static esp_err_t accept_command_result(int fd, const protocol_message_t *message)
{
    char error[PROTOCOL_ERROR_MAX] = {0};
    protocol_status_t status = PROTOCOL_ERROR_HELLO_REQUIRED;
    bool matched = false;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return ESP_FAIL;
    int host = find_host_by_fd_locked(fd);
    if (host >= 0) {
        status = protocol_session_accept(&s_hosts[host].protocol, message, error, sizeof(error));
        if (status == PROTOCOL_OK) {
            for (int i = 0; i < PENDING_COMMAND_COUNT; ++i) {
                if (s_pending[i].used && s_pending[i].fd == fd &&
                    strcmp(s_pending[i].request_id,
                           message->data.command_result.request_id) == 0) {
                    s_pending[i].used = false;
                    matched = true;
                    break;
                }
            }
        }
    }
    xSemaphoreGive(s_mutex);
    if (status != PROTOCOL_OK) {
        rate_limited_protocol_log(fd, status, error);
        return ESP_OK;
    }
    if (!matched) {
        rate_limited_protocol_log(fd, PROTOCOL_ERROR_SCHEMA, "unknown_request_id");
        return ESP_OK;
    }

    const protocol_command_result_t *result = &message->data.command_result;
    app_state_set_command_result(s_app_state, result->request_id, result->success,
                                 result->write_succeeded, result->confirmed, result->error);
    ESP_LOGI(TAG, "Command %s: success=%s write=%s confirmed=%s",
             result->request_id, result->success ? "yes" : "no",
             result->write_succeeded ? "yes" : "no", result->confirmed ? "yes" : "no");
    app_state_set_last_message(s_app_state,
        result->confirmed ? "DISPLAY SWITCH CONFIRMED" :
        (result->write_succeeded ? "SWITCH WRITTEN NOT CONFIRMED" : "DISPLAY SWITCH FAILED"));
    return ESP_OK;
}

static esp_err_t websocket_handler(httpd_req_t *request)
{
    // ESP-IDF v6.1 completes the HTTP Upgrade before invoking this frame handler.
    httpd_ws_frame_t frame = {0};
    esp_err_t result = httpd_ws_recv_frame(request, &frame, 0);
    if (result != ESP_OK) return result;
    int fd = httpd_req_to_sockfd(request);
    if (frame.len > PROTOCOL_MAX_MESSAGE_BYTES) {
        rate_limited_protocol_log(fd, PROTOCOL_ERROR_TOO_LONG, "message_too_long");
        return ESP_FAIL;
    }
    if (frame.type == HTTPD_WS_TYPE_PONG) {
        uint8_t pong_payload[126];
        if (frame.len > sizeof(pong_payload)) return ESP_FAIL;
        if (frame.len > 0) {
            frame.payload = pong_payload;
            return httpd_ws_recv_frame(request, &frame, frame.len);
        }
        return ESP_OK;
    }
    if (frame.len == 0) {
        rate_limited_protocol_log(fd, PROTOCOL_ERROR_SCHEMA, "empty_message");
        return ESP_OK;
    }
    if (frame.type != HTTPD_WS_TYPE_TEXT || frame.fragmented || !frame.final) {
        rate_limited_protocol_log(fd, PROTOCOL_ERROR_SCHEMA, "text_unfragmented_required");
        return ESP_FAIL;
    }

    char *payload = malloc(frame.len + 1);
    if (payload == NULL) return ESP_ERR_NO_MEM;
    frame.payload = (uint8_t *)payload;
    result = httpd_ws_recv_frame(request, &frame, frame.len);
    if (result != ESP_OK) {
        free(payload);
        return result;
    }
    payload[frame.len] = '\0';

    protocol_message_t message;
    char error[PROTOCOL_ERROR_MAX] = {0};
    protocol_status_t status = protocol_parse_message(payload, frame.len, &message,
                                                       error, sizeof(error));
    free(payload);
    if (status != PROTOCOL_OK) {
        rate_limited_protocol_log(fd, status, error);
        return ESP_OK;
    }
    if (message.type == PROTOCOL_MESSAGE_HELLO) return accept_hello(fd, &message);
    if (message.type == PROTOCOL_MESSAGE_TELEMETRY) return accept_telemetry(fd, &message);
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
    esp_err_t result = httpd_ws_send_frame_async(send->server, send->fd, &frame);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Command send failed: %s", esp_err_to_name(result));
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            clear_pending_request_locked(send->fd, send->request_id);
            xSemaphoreGive(s_mutex);
        }
        app_state_set_command_failed(s_app_state, send->request_id, COMMAND_SEND_FAILED,
                                     "websocket_send_failed");
        app_state_set_last_message(s_app_state, "DISPLAY COMMAND SEND FAILED");
    }
    free(send);
}

esp_err_t websocket_server_init(app_state_t *state)
{
    if (state == NULL) return ESP_ERR_INVALID_ARG;
    s_app_state = state;
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) return ESP_ERR_NO_MEM;
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
    config.open_fn = websocket_open;
    config.close_fn = websocket_close;
    esp_err_t result = httpd_start(&s_server, &config);
    if (result != ESP_OK) {
        s_server = NULL;
        return result;
    }
    const httpd_uri_t websocket_uri = {
        .uri = WEBSOCKET_SERVER_PATH,
        .method = HTTP_GET,
        .handler = websocket_handler,
        .is_websocket = true,
        .handle_ws_control_frames = false,
    };
    result = httpd_register_uri_handler(s_server, &websocket_uri);
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

void websocket_server_maintenance(int64_t now_monotonic_ms)
{
    char timed_out_request[PROTOCOL_REQUEST_ID_MAX + 1] = {0};
    if (s_mutex == NULL || xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return;
    for (int i = 0; i < PENDING_COMMAND_COUNT; ++i) {
        if (s_pending[i].used && now_monotonic_ms >= s_pending[i].deadline_ms) {
            s_pending[i].used = false;
            snprintf(timed_out_request, sizeof(timed_out_request), "%s",
                     s_pending[i].request_id);
        }
    }
    xSemaphoreGive(s_mutex);
    if (timed_out_request[0] != '\0') {
        app_state_set_command_failed(s_app_state, timed_out_request, COMMAND_TIMEOUT,
                                     "command_timeout");
        app_state_set_last_message(s_app_state, "DISPLAY COMMAND TIMED OUT");
    }
}

esp_err_t websocket_server_send_switch_display(host_id_t destination_host,
                                               const char *target,
                                               char *request_id,
                                               size_t request_id_capacity)
{
    if (destination_host != HOST_MAC || target == NULL || request_id == NULL ||
        request_id_capacity == 0 || (strcmp(target, "mac") != 0 &&
                                     strcmp(target, "windows") != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    async_send_t *send = calloc(1, sizeof(*send));
    if (send == NULL) return ESP_ERR_NO_MEM;

    int pending_index = -1;
    if (s_mutex == NULL || xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        free(send);
        return ESP_ERR_INVALID_STATE;
    }
    host_connection_t *connection = &s_hosts[destination_host];
    if (s_server == NULL || connection->fd < 0 || !connection->protocol.hello_received) {
        xSemaphoreGive(s_mutex);
        free(send);
        return ESP_ERR_INVALID_STATE;
    }
    for (int i = 0; i < PENDING_COMMAND_COUNT; ++i) {
        if (!s_pending[i].used) {
            pending_index = i;
            break;
        }
    }
    if (pending_index < 0) {
        xSemaphoreGive(s_mutex);
        free(send);
        return ESP_ERR_NO_MEM;
    }
    ++s_request_counter;
    char generated[PROTOCOL_REQUEST_ID_MAX + 1];
    snprintf(generated, sizeof(generated), "esp32-%08" PRIx32 "-%08" PRIx32,
             (uint32_t)monotonic_ms(), s_request_counter);
    if (strlen(generated) >= request_id_capacity ||
        !protocol_build_switch_command(send->payload, sizeof(send->payload), generated,
                                       target, connection->protocol.device_id)) {
        xSemaphoreGive(s_mutex);
        free(send);
        return ESP_ERR_INVALID_SIZE;
    }
    send->server = s_server;
    send->fd = connection->fd;
    pending_command_t *pending = &s_pending[pending_index];
    pending->used = true;
    pending->fd = connection->fd;
    pending->deadline_ms = monotonic_ms() + COMMAND_TIMEOUT_MS;
    snprintf(pending->request_id, sizeof(pending->request_id), "%s", generated);
    snprintf(send->request_id, sizeof(send->request_id), "%s", generated);
    snprintf(request_id, request_id_capacity, "%s", generated);
    xSemaphoreGive(s_mutex);

    app_state_set_command_pending(s_app_state, generated);

    esp_err_t result = httpd_queue_work(send->server, async_send_work, send);
    if (result != ESP_OK) {
        if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
            clear_pending_request_locked(send->fd, send->request_id);
            xSemaphoreGive(s_mutex);
        }
        app_state_set_command_failed(s_app_state, send->request_id, COMMAND_SEND_FAILED,
                                     "queue_send_failed");
        free(send);
    }
    return result;
}
