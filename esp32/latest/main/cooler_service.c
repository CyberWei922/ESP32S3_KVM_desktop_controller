#include "cooler_service.h"

#include <math.h>

#include "app_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hardware.h"
#include "nvs.h"

typedef enum {
    COOLER_COMMAND_SINGLE = 0,
    COOLER_COMMAND_DOUBLE,
    COOLER_COMMAND_NEXT_MODE,
    COOLER_COMMAND_POWER_OFF,
} cooler_command_t;

static const char *TAG = "cooler";
static app_state_t *s_state;
static QueueHandle_t s_commands;
static TaskHandle_t s_task;
static uint8_t s_last_on_level = 1;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }
static void wait_ms(unsigned milliseconds) { vTaskDelay(pdMS_TO_TICKS(milliseconds)); }

static esp_err_t relay_press(unsigned duration_ms)
{
    esp_err_t result = hardware_set_cooler_relay(true);
    if (result == ESP_OK) wait_ms(duration_ms);
    const esp_err_t release = hardware_set_cooler_relay(false);
    return result != ESP_OK ? result : release;
}

static esp_err_t cooler_click(void) { return relay_press(APP_COOLER_SINGLE_CLICK_MS); }

static esp_err_t cooler_double_click(void)
{
    esp_err_t result = relay_press(APP_COOLER_DOUBLE_CLICK_MS);
    if (result != ESP_OK) return result;
    wait_ms(APP_COOLER_DOUBLE_GAP_MS);
    return relay_press(APP_COOLER_DOUBLE_CLICK_MS);
}

static void save_preferences(void)
{
    app_state_snapshot_t snapshot;
    if (!app_state_get_snapshot(s_state, &snapshot)) return;
    nvs_handle_t handle;
    if (nvs_open("cooler", NVS_READWRITE, &handle) != ESP_OK) return;
    (void)nvs_set_u8(handle, "mode", (uint8_t)snapshot.thermal_mode);
    (void)nvs_set_i16(handle, "standard", snapshot.thermal_targets_c[THERMAL_STANDARD]);
    (void)nvs_set_i16(handle, "silent", snapshot.thermal_targets_c[THERMAL_SILENT]);
    (void)nvs_set_i16(handle, "turbo", snapshot.thermal_targets_c[THERMAL_TURBO]);
    (void)nvs_commit(handle);
    nvs_close(handle);
}

static void load_preferences(void)
{
    nvs_handle_t handle;
    if (nvs_open("cooler", NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t mode = THERMAL_MANUAL;
    int16_t target = 0;
    if (nvs_get_u8(handle, "mode", &mode) == ESP_OK && mode < THERMAL_MODE_COUNT) {
        app_state_set_thermal_mode(s_state, (thermal_mode_t)mode);
    }
    if (nvs_get_i16(handle, "standard", &target) == ESP_OK)
        app_state_set_thermal_target(s_state, THERMAL_STANDARD, target);
    if (nvs_get_i16(handle, "silent", &target) == ESP_OK)
        app_state_set_thermal_target(s_state, THERMAL_SILENT, target);
    if (nvs_get_i16(handle, "turbo", &target) == ESP_OK)
        app_state_set_thermal_target(s_state, THERMAL_TURBO, target);
    nvs_close(handle);
}

static bool set_level_up(uint8_t current)
{
    if (current >= 4 || cooler_click() != ESP_OK) return false;
    app_state_set_cooler_status(s_state, (cooler_state_t)(COOLER_LEVEL_1 + current),
                                current + 1, true, "COOLER LEVEL UP");
    return true;
}

static bool set_level_down(uint8_t current)
{
    if (current <= 1 || cooler_double_click() != ESP_OK) return false;
    app_state_set_cooler_status(s_state, (cooler_state_t)(COOLER_LEVEL_1 + current - 2),
                                current - 1, true, "COOLER LEVEL DOWN");
    return true;
}

static void manual_level_up(uint8_t current)
{
    if (current < 1 || current > 4 || cooler_click() != ESP_OK) return;
    const uint8_t next = current == 4 ? 1 : current + 1;
    app_state_set_cooler_status(s_state,
                                (cooler_state_t)(COOLER_LEVEL_1 + next - 1),
                                next, true, "COOLER LEVEL UP");
}

static bool initialize_cooler(void)
{
    app_state_set_cooler_status(s_state, COOLER_INITIALIZING, 0, false,
                                "CALIBRATING COOLER");
    wait_ms(APP_COOLER_BOOT_SETTLE_MS);
    if (relay_press(APP_COOLER_POWER_HOLD_MS) != ESP_OK) return false;
    wait_ms(APP_COOLER_ACTION_SETTLE_MS);
    if (cooler_click() != ESP_OK) return false;
    wait_ms(APP_COOLER_ACTION_SETTLE_MS);
    for (int i = 0; i < 3; ++i) {
        if (cooler_double_click() != ESP_OK) return false;
        if (i != 2) wait_ms(APP_COOLER_DOUBLE_GROUP_GAP_MS);
    }
    s_last_on_level = 1;
    app_state_set_cooler_status(s_state, COOLER_LEVEL_1, 1, true, "COOLER READY L1");
    return true;
}

static void handle_command(cooler_command_t command)
{
    app_state_snapshot_t snapshot;
    if (!app_state_get_snapshot(s_state, &snapshot)) return;
    if (command == COOLER_COMMAND_NEXT_MODE) {
        const thermal_mode_t next = (thermal_mode_t)((snapshot.thermal_mode + 1) %
                                                     THERMAL_MODE_COUNT);
        app_state_set_thermal_mode(s_state, next);
        save_preferences();
        return;
    }
    if (command == COOLER_COMMAND_POWER_OFF) {
        if (snapshot.cooler_state == COOLER_OFF) return;
        if (snapshot.cooler_level >= 1 && snapshot.cooler_level <= 4)
            s_last_on_level = snapshot.cooler_level;
        if (relay_press(APP_COOLER_POWER_HOLD_MS) == ESP_OK)
            app_state_set_cooler_status(s_state, COOLER_OFF, 0, false, "COOLER OFF");
        else
            app_state_set_cooler_status(s_state, COOLER_ERROR, 0, false, "COOLER RELAY ERROR");
        return;
    }
    if (snapshot.cooler_state == COOLER_OFF && command == COOLER_COMMAND_SINGLE) {
        if (cooler_click() == ESP_OK)
            app_state_set_cooler_status(s_state,
                (cooler_state_t)(COOLER_LEVEL_1 + s_last_on_level - 1),
                s_last_on_level, true, "COOLER ON");
        return;
    }
    if (snapshot.thermal_mode == THERMAL_MANUAL) {
        if (command == COOLER_COMMAND_SINGLE) manual_level_up(snapshot.cooler_level);
        else if (command == COOLER_COMMAND_DOUBLE) (void)set_level_down(snapshot.cooler_level);
        return;
    }
    if (command == COOLER_COMMAND_SINGLE) {
        (void)app_state_adjust_thermal_target(s_state, APP_COOLER_TARGET_STEP_C);
        save_preferences();
    } else if (command == COOLER_COMMAND_DOUBLE) {
        (void)app_state_adjust_thermal_target(s_state, -APP_COOLER_TARGET_STEP_C);
        save_preferences();
    }
}

static void automatic_control(int64_t time_ms, float *filtered, int64_t *above_since,
                              int64_t *below_since, int64_t *last_change,
                              uint64_t *last_sequence)
{
    app_state_snapshot_t snapshot;
    if (!app_state_get_snapshot(s_state, &snapshot) ||
        snapshot.thermal_mode == THERMAL_MANUAL || !snapshot.cooler_control_enabled ||
        snapshot.cooler_level < 1 || snapshot.cooler_level > 4) return;
    const host_status_t *host = &snapshot.hosts[snapshot.display_target];
    if (!host->online || host->stale || !host->temperature_valid ||
        host->sequence == *last_sequence) return;
    *last_sequence = host->sequence;
    *filtered = isnan(*filtered) ? host->cpu_temperature_c
                                : (*filtered * 0.85f + host->cpu_temperature_c * 0.15f);
    const float target = snapshot.thermal_targets_c[snapshot.thermal_mode];
    if (*filtered > target + APP_COOLER_HYSTERESIS_C) {
        *below_since = 0;
        if (*above_since == 0) *above_since = time_ms;
        if (time_ms - *above_since >= APP_COOLER_UP_CONFIRM_MS &&
            time_ms - *last_change >= APP_COOLER_MIN_HOLD_MS &&
            set_level_up(snapshot.cooler_level)) {
            *last_change = time_ms;
            *above_since = 0;
        }
    } else if (*filtered < target - APP_COOLER_HYSTERESIS_C) {
        *above_since = 0;
        if (*below_since == 0) *below_since = time_ms;
        if (time_ms - *below_since >= APP_COOLER_DOWN_CONFIRM_MS &&
            time_ms - *last_change >= APP_COOLER_MIN_HOLD_MS &&
            set_level_down(snapshot.cooler_level)) {
            *last_change = time_ms;
            *below_since = 0;
        }
    } else {
        *above_since = 0;
        *below_since = 0;
    }
}

static void cooler_task(void *argument)
{
    (void)argument;
    load_preferences();
    if (!initialize_cooler()) {
        app_state_set_cooler_status(s_state, COOLER_ERROR, 0, false, "COOLER CALIBRATION ERROR");
    }
    float filtered = NAN;
    int64_t above_since = 0, below_since = 0, last_change = now_ms();
    uint64_t last_sequence = 0;
    for (;;) {
        cooler_command_t command;
        while (xQueueReceive(s_commands, &command, 0) == pdTRUE) handle_command(command);
        automatic_control(now_ms(), &filtered, &above_since, &below_since,
                          &last_change, &last_sequence);
        wait_ms(20);
    }
}

esp_err_t cooler_service_start(app_state_t *state)
{
    if (state == NULL) return ESP_ERR_INVALID_ARG;
    s_state = state;
    s_commands = xQueueCreate(8, sizeof(cooler_command_t));
    if (s_commands == NULL) return ESP_ERR_NO_MEM;
    if (xTaskCreate(cooler_task, "cooler", 4096, NULL, 5, &s_task) != pdPASS) {
        vQueueDelete(s_commands);
        s_commands = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Cooler control enabled; calibration scheduled");
    return ESP_OK;
}

void cooler_service_handle_event(app_event_type_t event)
{
    if (s_commands == NULL) return;
    cooler_command_t command;
    switch (event) {
        case APP_EVENT_RIGHT_SINGLE: command = COOLER_COMMAND_SINGLE; break;
        case APP_EVENT_RIGHT_DOUBLE: command = COOLER_COMMAND_DOUBLE; break;
        case APP_EVENT_RIGHT_MODE: command = COOLER_COMMAND_NEXT_MODE; break;
        case APP_EVENT_RIGHT_LONG: command = COOLER_COMMAND_POWER_OFF; break;
        default: return;
    }
    if (xQueueSend(s_commands, &command, 0) != pdTRUE)
        ESP_LOGW(TAG, "Cooler command queue full");
}
