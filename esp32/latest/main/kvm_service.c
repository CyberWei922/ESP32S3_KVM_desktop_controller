#include "kvm_service.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "button_service.h"
#include "cooler_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "hardware.h"
#include "websocket_server.h"

typedef struct {
    app_state_t *state;
    QueueHandle_t events;
    TaskHandle_t task;
    host_id_t pending_target;
    bool pending_confirmed;
    char pending_request_id[APP_STATE_DEVICE_ID_MAX + 1];
    int64_t display_deadline_ms;
    int64_t cooldown_started_ms;
    int64_t cooldown_deadline_ms;
    bool input_locked;
    bool wake_after_sleep;
    uint8_t sleeping_hosts;
    uint8_t last_sleep_failed_hosts;
    uint8_t power_pending_hosts;
    uint8_t power_failed_hosts;
    host_id_t wake_target;
    host_id_t switch_source;
    char power_request_id[HOST_COUNT][APP_STATE_DEVICE_ID_MAX + 1];
    enum {
        POWER_FLOW_IDLE = 0,
        POWER_FLOW_GLOBAL_SLEEP,
        POWER_FLOW_SCREEN_WAKE,
        POWER_FLOW_SWITCH_WAKE,
    } power_flow;
} service_t;

static const char *TAG = "kvm_service";
static service_t s_service;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void set_error(const char *message)
{
    app_state_set_kvm_phase(s_service.state, KVM_ERROR, message);
}

static bool set_backlight(bool enable)
{
    const esp_err_t result = hardware_set_backlight(enable);
    if (result == ESP_OK) {
        app_state_set_backlight(s_service.state, enable);
        return true;
    } else {
        app_state_set_last_message(s_service.state, "BACKLIGHT CONTROL ERROR");
        return false;
    }
}

static bool send_power_command(host_id_t host, display_power_action_t action)
{
    if (!websocket_server_host_supports_display_power(host, action)) return false;
    const esp_err_t result = websocket_server_send_display_power(
        host, action, s_service.power_request_id[host],
        sizeof(s_service.power_request_id[host]));
    if (result != ESP_OK) return false;
    s_service.power_pending_hosts |= (uint8_t)(1U << host);
    return true;
}

static void begin_global_sleep(void)
{
    app_state_snapshot_t snapshot;
    if (!app_state_get_snapshot(s_service.state, &snapshot)) return;
    if (snapshot.kvm_phase != KVM_IDLE && snapshot.kvm_phase != KVM_ERROR) {
        app_state_set_last_message(s_service.state, "SLEEP BLOCKED: KVM BUSY");
        return;
    }
    if (!set_backlight(false)) return;

    s_service.input_locked = true;
    s_service.wake_target = snapshot.display_target;
    s_service.power_flow = POWER_FLOW_GLOBAL_SLEEP;
    s_service.power_pending_hosts = 0;
    s_service.power_failed_hosts = 0;
    s_service.last_sleep_failed_hosts = 0;
    memset(s_service.power_request_id, 0, sizeof(s_service.power_request_id));
    for (int host = 0; host < HOST_COUNT; ++host) {
        if (!snapshot.hosts[host].online || snapshot.hosts[host].stale ||
            !send_power_command((host_id_t)host, DISPLAY_POWER_SLEEP)) {
            s_service.power_failed_hosts |= (uint8_t)(1U << host);
        }
    }
    app_state_set_last_message(s_service.state,
        s_service.power_pending_hosts != 0
            ? "REQUESTING HOST DISPLAY SLEEP"
            : "BACKLIGHT OFF; CLIENT UPGRADE REQUIRED");
    if (s_service.power_pending_hosts == 0) {
        s_service.last_sleep_failed_hosts = s_service.power_failed_hosts;
        s_service.power_flow = POWER_FLOW_IDLE;
    }
}

static void begin_screen_wake(void)
{
    if (!set_backlight(true)) return;
    s_service.power_flow = POWER_FLOW_SCREEN_WAKE;
    s_service.power_pending_hosts = 0;
    s_service.power_failed_hosts = 0;
    memset(s_service.power_request_id, 0, sizeof(s_service.power_request_id));
    const host_id_t target = s_service.wake_target;
    if ((s_service.sleeping_hosts & (1U << target)) == 0) {
        s_service.input_locked = false;
        s_service.power_flow = POWER_FLOW_IDLE;
        app_state_set_last_message(s_service.state,
                                   "BACKLIGHT ON; HOST SLEEP WAS PARTIAL");
        return;
    }
    if (!send_power_command(target, DISPLAY_POWER_WAKE)) {
        s_service.input_locked = false;
        s_service.power_flow = POWER_FLOW_IDLE;
        app_state_set_last_message(s_service.state, "WAKE FAILED: TARGET CLIENT OFFLINE");
        return;
    }
    app_state_set_last_message(s_service.state, "WAKING CURRENT DISPLAY HOST");
}

static void handle_backlight_toggle(void)
{
    if (s_service.input_locked && !hardware_backlight_enabled()) {
        if (s_service.power_flow == POWER_FLOW_GLOBAL_SLEEP) {
            s_service.wake_after_sleep = true;
            return;
        }
        begin_screen_wake();
    } else if (!s_service.input_locked) {
        begin_global_sleep();
    }
}

static void handle_usb_correction(void)
{
    if (!APP_KVM_TIMING_CONFIRMED) {
        app_state_set_last_message(s_service.state, "USB CORRECTION LOCKED");
        return;
    }
    app_state_snapshot_t snapshot;
    if (!app_state_get_snapshot(s_service.state, &snapshot)) return;
    if (snapshot.kvm_phase != KVM_IDLE && snapshot.kvm_phase != KVM_ERROR) {
        app_state_set_last_message(s_service.state, "USB CORRECTION: KVM BUSY");
        return;
    }
    app_state_set_kvm_phase(s_service.state, KVM_PULSING_USB, "CORRECTING USB STATE");
    const esp_err_t result = hardware_pulse_usb_relay(APP_USB_RELAY_PULSE_MS);
    if (result != ESP_OK) {
        set_error("USB CORRECTION FAILED");
        return;
    }
    app_state_commit_usb_correction(s_service.state);
    app_state_set_kvm_phase(s_service.state, KVM_IDLE, "USB STATE CORRECTED");
}

static void send_switch_ddc(host_id_t current, host_id_t target)
{
    app_state_set_kvm_phase(s_service.state, KVM_SENDING_DDC, "SENDING DISPLAY COMMAND");
    const char *target_name = target == HOST_MAC ? "mac" : "windows";
    const esp_err_t result = websocket_server_send_switch_display(
        current, target_name, s_service.pending_request_id,
        sizeof(s_service.pending_request_id));
    if (result != ESP_OK) {
        set_error("DISPLAY COMMAND NOT SENT");
        return;
    }
    s_service.pending_target = target;
    s_service.pending_confirmed = false;
    app_state_set_kvm_phase(s_service.state, KVM_WAITING_DDC_RESULT, "WAITING FOR DDC RESULT");
}

static void begin_switch(void)
{
    if (!APP_KVM_TIMING_CONFIRMED) {
        app_state_set_kvm_phase(s_service.state, KVM_DISABLED_UNCONFIRMED_TIMING,
                                "KVM LOCKED: TIMING REQUIRED");
        return;
    }
    app_state_snapshot_t snapshot;
    if (!app_state_get_snapshot(s_service.state, &snapshot)) return;
    if (snapshot.kvm_phase != KVM_IDLE && snapshot.kvm_phase != KVM_ERROR) {
        return; /* Ignore and never queue requests while switching or cooling. */
    }
    const host_id_t current = snapshot.display_target;
    const host_id_t target = current == HOST_MAC ? HOST_WINDOWS : HOST_MAC;
#if APP_MAC_TO_WINDOWS_TEST_MODE
    if (current != HOST_MAC || target != HOST_WINDOWS) {
        set_error("TEST MODE: MAC TO WINDOWS ONLY");
        return;
    }
    if (!snapshot.hosts[HOST_MAC].online || snapshot.hosts[HOST_MAC].stale) {
        set_error("KVM BLOCKED: MAC OFFLINE");
        return;
    }
#else
    if (!snapshot.hosts[current].online || snapshot.hosts[current].stale ||
        !snapshot.hosts[target].online || snapshot.hosts[target].stale) {
        set_error("KVM BLOCKED: HOST OFFLINE");
        return;
    }
#endif
    if ((s_service.sleeping_hosts & (1U << target)) != 0) {
        s_service.power_flow = POWER_FLOW_SWITCH_WAKE;
        s_service.power_pending_hosts = 0;
        s_service.power_failed_hosts = 0;
        s_service.switch_source = current;
        s_service.pending_target = target;
        memset(s_service.power_request_id, 0, sizeof(s_service.power_request_id));
        app_state_set_kvm_phase(s_service.state, KVM_SENDING_DDC, "WAKING TARGET HOST");
        if (!send_power_command(target, DISPLAY_POWER_WAKE)) {
            s_service.power_flow = POWER_FLOW_IDLE;
            set_error("TARGET DISPLAY WAKE NOT SENT");
        }
        return;
    }
    send_switch_ddc(current, target);
}

static void handle_event(const app_event_t *event)
{
    if (s_service.input_locked) {
        if (event->type == APP_EVENT_LEFT_LONG && !hardware_backlight_enabled()) {
            handle_backlight_toggle();
        }
        return;
    }
    switch (event->type) {
        case APP_EVENT_LEFT_SINGLE:
            begin_switch();
            break;
        case APP_EVENT_LEFT_DOUBLE:
            handle_usb_correction();
            break;
        case APP_EVENT_LEFT_LONG:
            handle_backlight_toggle();
            break;
        case APP_EVENT_MIDDLE_SINGLE:
            app_state_next_data_window(s_service.state);
            break;
        case APP_EVENT_MIDDLE_DOUBLE:
            app_state_previous_data_window(s_service.state);
            break;
        case APP_EVENT_RIGHT_SINGLE:
        case APP_EVENT_RIGHT_DOUBLE:
        case APP_EVENT_RIGHT_MODE:
        case APP_EVENT_RIGHT_LONG:
            cooler_service_handle_event(event->type);
            break;
        case APP_EVENT_MIDDLE_LONG:
            /* Explicitly unassigned. */
            break;
    }
}

static void finish_power_flow_if_ready(void)
{
    if (s_service.power_pending_hosts != 0) return;
    const bool failed = s_service.power_failed_hosts != 0;
    switch (s_service.power_flow) {
        case POWER_FLOW_GLOBAL_SLEEP:
            s_service.last_sleep_failed_hosts = s_service.power_failed_hosts;
            app_state_set_last_message(s_service.state,
                failed ? "DISPLAY SLEEP PARTIAL/FAILED" : "HOST DISPLAYS SLEEPING");
            s_service.power_flow = POWER_FLOW_IDLE;
            if (s_service.wake_after_sleep) {
                s_service.wake_after_sleep = false;
                begin_screen_wake();
            }
            break;
        case POWER_FLOW_SCREEN_WAKE:
            s_service.input_locked = false;
            app_state_set_last_message(s_service.state,
                failed ? "DISPLAY WAKE FAILED" :
                (s_service.last_sleep_failed_hosts != 0
                    ? "WAKE OK; PREVIOUS SLEEP PARTIAL"
                    : "DISPLAY OUTPUT RESTORED"));
            s_service.power_flow = POWER_FLOW_IDLE;
            break;
        case POWER_FLOW_SWITCH_WAKE: {
            const host_id_t source = s_service.switch_source;
            const host_id_t target = s_service.pending_target;
            s_service.power_flow = POWER_FLOW_IDLE;
            if (failed) {
                set_error("TARGET DISPLAY WAKE FAILED");
            } else {
                send_switch_ddc(source, target);
            }
            break;
        }
        case POWER_FLOW_IDLE:
            break;
    }
}

static void consume_power_results(void)
{
    display_power_result_event_t event;
    while (websocket_server_take_display_power_result(&event)) {
        if (event.host >= HOST_COUNT ||
            strcmp(event.request_id, s_service.power_request_id[event.host]) != 0) {
            continue;
        }
        const uint8_t bit = (uint8_t)(1U << event.host);
        s_service.power_pending_hosts &= (uint8_t)~bit;
        if (event.success) {
            if (event.action == DISPLAY_POWER_SLEEP) {
                s_service.sleeping_hosts |= bit;
            } else {
                s_service.sleeping_hosts &= (uint8_t)~bit;
            }
        } else {
            s_service.power_failed_hosts |= bit;
        }
        s_service.power_request_id[event.host][0] = '\0';
        finish_power_flow_if_ready();
    }
}

static void advance_state_machine(int64_t time_ms)
{
    app_state_snapshot_t snapshot;
    if (!app_state_get_snapshot(s_service.state, &snapshot)) return;
    if (snapshot.kvm_phase == KVM_WAITING_DDC_RESULT &&
        strcmp(snapshot.command_request_id, s_service.pending_request_id) == 0) {
        if (snapshot.command_status == COMMAND_RESULT) {
            if (!snapshot.command_success || !snapshot.command_write_succeeded) {
                set_error("DDC WRITE FAILED: USB UNCHANGED");
                return;
            }
            s_service.pending_confirmed = snapshot.command_confirmed;
            if (APP_KVM_COOLDOWN_START_POLICY == APP_COOLDOWN_START_DDC_SUCCESS) {
                s_service.cooldown_started_ms = time_ms;
            }
            s_service.display_deadline_ms = time_ms + APP_DDC_TO_USB_DELAY_MS;
            app_state_set_kvm_phase(s_service.state, KVM_WAITING_DISPLAY,
                                    snapshot.command_confirmed
                                        ? "DISPLAY CONFIRMED; WAITING"
                                        : "DDC WRITTEN; UNCONFIRMED");
        } else if (snapshot.command_status == COMMAND_TIMEOUT ||
                   snapshot.command_status == COMMAND_DISCONNECTED ||
                   snapshot.command_status == COMMAND_SEND_FAILED ||
                   snapshot.command_status == COMMAND_REJECTED) {
            set_error("DDC COMMAND FAILED: USB UNCHANGED");
        }
        return;
    }
    if (snapshot.kvm_phase == KVM_WAITING_DISPLAY && time_ms >= s_service.display_deadline_ms) {
        if (APP_KVM_COOLDOWN_START_POLICY == APP_COOLDOWN_START_USB_PULSE) {
            s_service.cooldown_started_ms = time_ms;
        }
        app_state_set_kvm_phase(s_service.state, KVM_PULSING_USB, "SWITCHING USB");
        const esp_err_t result = hardware_pulse_usb_relay(APP_USB_RELAY_PULSE_MS);
        if (result != ESP_OK) {
            set_error("USB RELAY ERROR");
            return;
        }
        app_state_commit_switch(s_service.state, s_service.pending_target,
                                s_service.pending_confirmed);
        if (APP_KVM_COOLDOWN_START_POLICY == APP_COOLDOWN_START_FLOW_COMPLETE) {
            s_service.cooldown_started_ms = now_ms();
        }
        s_service.cooldown_deadline_ms = s_service.cooldown_started_ms + APP_KVM_COOLDOWN_MS;
        app_state_set_kvm_phase(s_service.state, KVM_COOLING, "KVM COOLDOWN");
        return;
    }
    if (snapshot.kvm_phase == KVM_COOLING && time_ms >= s_service.cooldown_deadline_ms) {
        app_state_set_kvm_phase(s_service.state, KVM_IDLE, "KVM READY");
    }
}

static void service_task(void *argument)
{
    (void)argument;
    app_event_t event;
    for (;;) {
        while (xQueueReceive(s_service.events, &event, 0) == pdTRUE) {
            handle_event(&event);
        }
        consume_power_results();
        advance_state_machine(now_ms());
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

esp_err_t kvm_service_start(app_state_t *state, QueueHandle_t event_queue)
{
    if (state == NULL || event_queue == NULL) return ESP_ERR_INVALID_ARG;
    if (s_service.task != NULL) return ESP_OK;
    s_service.state = state;
    s_service.events = event_queue;
    if (xTaskCreate(service_task, "kvm_state", 4096, NULL, 6,
                    &s_service.task) != pdPASS) {
        s_service.task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "KVM physical switching: %s",
             APP_KVM_TIMING_CONFIRMED ? "enabled" : "locked pending timing measurements");
    return ESP_OK;
}
