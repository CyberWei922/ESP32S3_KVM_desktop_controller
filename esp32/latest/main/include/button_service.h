#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    APP_EVENT_LEFT_SINGLE = 0,
    APP_EVENT_LEFT_DOUBLE,
    APP_EVENT_LEFT_LONG,
    APP_EVENT_MIDDLE_SINGLE,
    APP_EVENT_MIDDLE_DOUBLE,
    APP_EVENT_MIDDLE_LONG,
    APP_EVENT_RIGHT_SINGLE,
    APP_EVENT_RIGHT_DOUBLE,
    APP_EVENT_RIGHT_MODE,
    APP_EVENT_RIGHT_LONG,
} app_event_type_t;

typedef struct {
    app_event_type_t type;
} app_event_t;

esp_err_t button_service_init(QueueHandle_t event_queue);
esp_err_t button_service_start(void);
