#include <inttypes.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"

#include "app_state.h"
#include "lcd_driver.h"
#include "lcd_test_ui.h"

#define APP_VERSION "0.2.0"

static const char *TAG = "kvm_controller";
static app_state_t s_app_state;

static void log_hardware_info(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size = 0;

    esp_chip_info(&chip_info);
    esp_err_t flash_result = esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "KVM Controller firmware %s", APP_VERSION);
    ESP_LOGI(TAG, "Target: %s, cores: %d, revision: v%d.%d",
             CONFIG_IDF_TARGET,
             chip_info.cores,
             chip_info.revision / 100,
             chip_info.revision % 100);
    ESP_LOGI(TAG, "Features: Wi-Fi=%s, BLE=%s",
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "yes" : "no",
             (chip_info.features & CHIP_FEATURE_BLE) ? "yes" : "no");

    if (flash_result == ESP_OK) {
        ESP_LOGI(TAG, "Flash size: %" PRIu32 " MB", flash_size / (1024 * 1024));
    } else {
        ESP_LOGE(TAG, "Failed to read flash size: %s", esp_err_to_name(flash_result));
    }

    size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM available to heap: %u MB",
             (unsigned int)(psram_size / (1024 * 1024)));
    ESP_LOGI(TAG, "Reset reason: %d", esp_reset_reason());
}

void app_main(void)
{
    app_state_init(&s_app_state);
    app_state_load_demo_data(&s_app_state);
    log_hardware_info();

    ESP_LOGI(TAG, "State model ready");
    ESP_LOGI(TAG, "Demo Mac: online=%s, CPU=%.1f C, memory=%.1f%%",
             s_app_state.hosts[HOST_MAC].online ? "yes" : "no",
             s_app_state.hosts[HOST_MAC].cpu_temperature_c,
             s_app_state.hosts[HOST_MAC].memory_percent);
    ESP_LOGI(TAG, "Demo Windows: online=%s, status=%s",
             s_app_state.hosts[HOST_WINDOWS].online ? "yes" : "no",
             s_app_state.hosts[HOST_WINDOWS].error);

    ESP_LOGI(TAG, "Initializing MD024-QVGA-01-V01 display");
    esp_err_t lcd_result = lcd_driver_init();
    if (lcd_result == ESP_OK) {
        lcd_test_ui_show();
        ESP_LOGI(TAG, "LCD test screen rendered");
    } else {
        ESP_LOGE(TAG, "LCD initialization failed: %s", esp_err_to_name(lcd_result));
    }
}
