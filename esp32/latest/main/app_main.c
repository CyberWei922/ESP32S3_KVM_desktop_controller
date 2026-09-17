#include <inttypes.h>

#include "app_config.h"
#include "app_state.h"
#include "button_service.h"
#include "cooler_service.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hardware.h"
#include "kvm_service.h"
#include "network_manager.h"
#include "nvs_flash.h"
#include "ui.h"
#include "websocket_server.h"

static const char *TAG = "kvm_controller";
static app_state_t s_state;
static QueueHandle_t s_event_queue;

static void log_hardware_info(void)
{
    esp_chip_info_t chip;
    uint32_t flash_size = 0;
    esp_chip_info(&chip);
    ESP_LOGI(TAG, "KVM Controller %s on %s, %d cores", APP_VERSION,
             CONFIG_IDF_TARGET, chip.cores);
    if (esp_flash_get_size(NULL, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "Flash: %" PRIu32 " MB; PSRAM heap: %u MB",
                 flash_size / (1024 * 1024),
                 (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / (1024 * 1024)));
    }
    ESP_LOGI(TAG, "Reset reason: %d", esp_reset_reason());
}

static void maintenance_task(void *argument)
{
    (void)argument;
    for (;;) {
        const int64_t time_ms = esp_timer_get_time() / 1000;
        const uint32_t stale = app_state_mark_stale_hosts(&s_state, time_ms);
        if (stale != 0) app_state_set_last_message(&s_state, "HOST TELEMETRY STALE");
        websocket_server_maintenance(time_ms);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void app_main(void)
{
    /* This must be the first hardware operation: no relay can follow UI or network init. */
    ESP_ERROR_CHECK(hardware_safe_init());
    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);
    ESP_ERROR_CHECK(app_state_init(&s_state));
    ESP_ERROR_CHECK(ui_start(&s_state));
    if (!ui_wait_ready(3000)) {
        ESP_LOGW(TAG, "Boot screen did not become ready before system initialization");
    }
    vTaskDelay(pdMS_TO_TICKS(APP_BOOT_BACKLIGHT_SETTLE_MS));

    (void)ui_set_boot_status("INITIALIZING SYSTEM");
    ESP_ERROR_CHECK(websocket_server_init(&s_state));
    s_event_queue = xQueueCreate(16, sizeof(app_event_t));
    if (s_event_queue == NULL) ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(button_service_init(s_event_queue));
    ESP_ERROR_CHECK(kvm_service_start(&s_state, s_event_queue));
    ESP_ERROR_CHECK(cooler_service_start(&s_state));
    ESP_ERROR_CHECK(button_service_start());
    log_hardware_info();

    (void)ui_set_boot_status("STARTING NETWORK");
    const esp_err_t network_result = network_manager_init(&s_state);
    if (network_result != ESP_OK) {
        ESP_LOGE(TAG, "Network initialization failed: %s", esp_err_to_name(network_result));
        app_state_set_last_message(&s_state, "NETWORK INITIALIZATION FAILED");
    }
    if (xTaskCreate(maintenance_task, "maintenance", 3072, NULL, 4, NULL) != pdPASS) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    vTaskDelay(pdMS_TO_TICKS(APP_BOOT_WIFI_SETTLE_MS));
    (void)ui_set_boot_status("RENDERING UI");
    vTaskDelay(pdMS_TO_TICKS(250));
    (void)ui_show_main_screen();
    ESP_LOGI(TAG, "System tasks started; cooler control enabled on GPIO8");
}
