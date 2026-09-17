#pragma once

#include "app_state.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

esp_err_t kvm_service_start(app_state_t *state, QueueHandle_t event_queue);
