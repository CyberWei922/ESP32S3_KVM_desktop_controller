#include "button_service.h"

#include <stdbool.h>
#include <stddef.h>

#include "app_config.h"
#include "button_gesture.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

typedef struct {
    gpio_num_t pin;
    app_event_type_t single_event;
    app_event_type_t double_event;
    app_event_type_t long_event;
    button_gesture_t gesture;
} button_t;

static const char *TAG = "buttons";
static QueueHandle_t s_event_queue;
static TaskHandle_t s_task;
static button_t s_buttons[] = {
    { .pin = APP_BUTTON_PIN_LEFT, .single_event = APP_EVENT_LEFT_SINGLE,
      .double_event = APP_EVENT_LEFT_DOUBLE, .long_event = APP_EVENT_LEFT_LONG },
    { .pin = APP_BUTTON_PIN_MIDDLE, .single_event = APP_EVENT_MIDDLE_SINGLE,
      .double_event = APP_EVENT_MIDDLE_DOUBLE, .long_event = APP_EVENT_MIDDLE_LONG },
    { .pin = APP_BUTTON_PIN_RIGHT, .single_event = APP_EVENT_RIGHT_SINGLE,
      .double_event = APP_EVENT_RIGHT_DOUBLE, .long_event = APP_EVENT_RIGHT_LONG },
};

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool is_pressed(gpio_num_t pin)
{
    return gpio_get_level(pin) == 0;
}

static void publish(app_event_type_t type)
{
    const app_event_t event = { .type = type };
    if (xQueueSend(s_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Semantic event queue full; gesture ignored");
    }
}

static void scan_button(button_t *button, int64_t time_ms)
{
    const bool right = button->pin == APP_BUTTON_PIN_RIGHT;
    const button_gesture_output_t output = right
        ? button_gesture_update_right(&button->gesture, is_pressed(button->pin), time_ms)
        : button_gesture_update(&button->gesture, is_pressed(button->pin), time_ms);
    switch (output) {
        case BUTTON_GESTURE_SINGLE: publish(button->single_event); break;
        case BUTTON_GESTURE_DOUBLE: publish(button->double_event); break;
        case BUTTON_GESTURE_LONG: publish(button->long_event); break;
        case BUTTON_GESTURE_MEDIUM_RELEASE: publish(APP_EVENT_RIGHT_MODE); break;
        case BUTTON_GESTURE_VERY_LONG: publish(APP_EVENT_RIGHT_LONG); break;
        case BUTTON_GESTURE_NONE: break;
    }
}

static void button_task(void *argument)
{
    (void)argument;
    for (;;) {
        const int64_t time_ms = now_ms();
        for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
            scan_button(&s_buttons[i], time_ms);
        }
        vTaskDelay(pdMS_TO_TICKS(APP_BUTTON_SCAN_MS));
    }
}

esp_err_t button_service_init(QueueHandle_t event_queue)
{
    if (event_queue == NULL) return ESP_ERR_INVALID_ARG;
    s_event_queue = event_queue;
    const gpio_config_t config = {
        .pin_bit_mask = (UINT64_C(1) << APP_BUTTON_PIN_LEFT) |
                        (UINT64_C(1) << APP_BUTTON_PIN_MIDDLE) |
                        (UINT64_C(1) << APP_BUTTON_PIN_RIGHT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "button GPIO init");
    const int64_t time_ms = now_ms();
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
        button_gesture_init(&s_buttons[i].gesture, is_pressed(s_buttons[i].pin), time_ms);
    }
    return ESP_OK;
}

esp_err_t button_service_start(void)
{
    if (s_event_queue == NULL) return ESP_ERR_INVALID_STATE;
    if (s_task != NULL) return ESP_OK;
    if (xTaskCreate(button_task, "buttons", 3072, NULL, 6, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
