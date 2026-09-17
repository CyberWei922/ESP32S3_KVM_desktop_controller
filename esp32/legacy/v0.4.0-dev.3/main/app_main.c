#include <inttypes.h>

#include "esp_chip_info.h"
#include "esp_check.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_state.h"
#include "buttons.h"
#include "lcd_driver.h"
#include "lcd_test_ui.h"
#include "network_manager.h"
#include "websocket_server.h"

#define APP_VERSION "0.4.0-dev.3"
#define USB_RELAY_TEST_PIN GPIO_NUM_7
#define COOLER_RELAY_TEST_PIN GPIO_NUM_8

static const char *TAG = "kvm_controller";
static app_state_t s_app_state;

static esp_err_t relay_test_init(void)
{
    // Preset both relay latches LOW before enabling either output. External 10k
    // pulldowns keep both T1 inputs released while the ESP32 is resetting.
    ESP_RETURN_ON_ERROR(gpio_set_level(USB_RELAY_TEST_PIN, 0), TAG,
                        "USB relay output preset failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(COOLER_RELAY_TEST_PIN, 0), TAG,
                        "cooler relay output preset failed");
    const gpio_config_t config = {
        .pin_bit_mask = (UINT64_C(1) << USB_RELAY_TEST_PIN) |
                        (UINT64_C(1) << COOLER_RELAY_TEST_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "relay test GPIO init failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(USB_RELAY_TEST_PIN, 0), TAG,
                        "USB relay safe level failed");
    return gpio_set_level(COOLER_RELAY_TEST_PIN, 0);
}

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
    uint8_t pressed_mask = 0;
    bool first_button_draw = true;
    int64_t next_status_update_ms = 0;
    app_state_snapshot_t snapshot;
    while (true) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (buttons_poll(now_ms, &pressed_mask) || first_button_draw) {
            const bool usb_relay_closed = (pressed_mask & BUTTON_MASK_LEFT) != 0;
            const bool cooler_relay_closed = (pressed_mask & BUTTON_MASK_RIGHT) != 0;
            gpio_set_level(USB_RELAY_TEST_PIN, usb_relay_closed ? 1 : 0);
            gpio_set_level(COOLER_RELAY_TEST_PIN, cooler_relay_closed ? 1 : 0);
            lcd_test_ui_update_buttons(pressed_mask);
            first_button_draw = false;
        }
        if (now_ms >= next_status_update_ms &&
            app_state_get_snapshot(&s_app_state, &snapshot)) {
            lcd_test_ui_update(&snapshot, frame++);
            next_status_update_ms = now_ms + 500;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
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
    ESP_ERROR_CHECK(buttons_init());
    ESP_ERROR_CHECK(relay_test_init());
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
