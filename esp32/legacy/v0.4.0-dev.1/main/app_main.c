#include <inttypes.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_state.h"
#include "lcd_driver.h"
#include "lcd_test_ui.h"
#include "network_manager.h"
#include "websocket_server.h"

#define APP_VERSION "0.4.0-dev"

static const char *TAG = "kvm_controller";
static app_state_t s_app_state;

static void log_hardware_info(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0;
    esp_chip_info(&chip_info);
    esp_err_t flash_result = esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "KVM Controller firmware %s", APP_VERSION);
    ESP_LOGI(TAG, "Target: %s, cores: %d, revision: v%d.%d", CONFIG_IDF_TARGET,
             chip_info.cores, chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "Features: Wi-Fi=%s, BLE=%s",
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "yes" : "no",
             (chip_info.features & CHIP_FEATURE_BLE) ? "yes" : "no");
    if (flash_result == ESP_OK) {
        ESP_LOGI(TAG, "Flash size: %" PRIu32 " MB", flash_size / (1024 * 1024));
    } else {
        ESP_LOGE(TAG, "Failed to read flash size: %s", esp_err_to_name(flash_result));
    }
    ESP_LOGI(TAG, "PSRAM available to heap: %u MB",
             (unsigned int)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / (1024 * 1024)));
    ESP_LOGI(TAG, "Reset reason: %d", esp_reset_reason());
}

static void display_task(void *argument)
{
    (void)argument;
    uint32_t frame = 0;
    app_state_snapshot_t snapshot;
    while (true) {
        if (app_state_get_snapshot(&s_app_state, &snapshot)) {
            lcd_test_ui_update(&snapshot, frame++);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void maintenance_task(void *argument)
{
    (void)argument;
    while (true) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        uint32_t stale_mask = app_state_mark_stale_hosts(&s_app_state, now_ms);
        if (stale_mask & (1U << HOST_MAC)) ESP_LOGW(TAG, "Mac telemetry is stale");
        if (stale_mask & (1U << HOST_WINDOWS)) ESP_LOGW(TAG, "Windows telemetry is stale");
        if (stale_mask != 0) app_state_set_last_message(&s_app_state, "HOST TELEMETRY STALE");
        websocket_server_maintenance(now_ms);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(app_state_init(&s_app_state));
    ESP_ERROR_CHECK(websocket_server_init(&s_app_state));
    log_hardware_info();

    ESP_LOGI(TAG, "Initializing MD024-QVGA-01-V01 display");
    esp_err_t lcd_result = lcd_driver_init();
    if (lcd_result == ESP_OK) {
        lcd_test_ui_show();
        if (xTaskCreate(display_task, "display", 4096, NULL, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Could not start display task");
        }
    } else {
        ESP_LOGE(TAG, "LCD initialization failed: %s", esp_err_to_name(lcd_result));
    }

    esp_err_t network_result = network_manager_init(&s_app_state);
    if (network_result != ESP_OK) {
        ESP_LOGE(TAG, "Network initialization failed: %s", esp_err_to_name(network_result));
        app_state_set_last_message(&s_app_state, "NETWORK INITIALIZATION FAILED");
    }
    if (xTaskCreate(maintenance_task, "maintenance", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Could not start maintenance task");
    }
    ESP_LOGI(TAG, "System ready; display refresh is 2 FPS");
}
