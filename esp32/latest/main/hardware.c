#include "hardware.h"

#include "app_config.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "hardware";
static bool s_backlight_enabled;

static esp_err_t configure_safe_low_output(gpio_num_t pin)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(pin, 0), TAG, "preset GPIO%d low", pin);
    const gpio_config_t config = {
        .pin_bit_mask = UINT64_C(1) << pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "configure GPIO%d", pin);
    return gpio_set_level(pin, 0);
}

esp_err_t hardware_safe_init(void)
{
    ESP_RETURN_ON_ERROR(configure_safe_low_output(APP_USB_RELAY_PIN), TAG,
                        "USB relay safe init");
    ESP_RETURN_ON_ERROR(configure_safe_low_output(APP_COOLER_RELAY_PIN), TAG,
                        "cooler relay safe init");

    /* Release the active-low BLK line before open-drain mode takes control.
     * The weak internal pull-up stabilizes the otherwise floating enable node;
     * GPIO15 still never drives BLK high in push-pull mode. */
    ESP_RETURN_ON_ERROR(gpio_set_level(APP_LCD_PIN_BACKLIGHT, 1), TAG,
                        "backlight preset released");
    const gpio_config_t backlight = {
        .pin_bit_mask = UINT64_C(1) << APP_LCD_PIN_BACKLIGHT,
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&backlight), TAG, "backlight open-drain init");
    s_backlight_enabled = true;
    ESP_LOGI(TAG, "GPIO7/8 released; GPIO15 open-drain RELEASED + WEAK PULL-UP");
    return ESP_OK;
}

esp_err_t hardware_set_backlight(bool enabled)
{
    /* HIGH in open-drain mode means high impedance/released, not push-pull HIGH. */
    esp_err_t result = gpio_set_level(APP_LCD_PIN_BACKLIGHT, enabled ? 1 : 0);
    if (result == ESP_OK) s_backlight_enabled = enabled;
    return result;
}

bool hardware_backlight_enabled(void)
{
    return s_backlight_enabled;
}

esp_err_t hardware_pulse_usb_relay(unsigned duration_ms)
{
    if (!APP_KVM_TIMING_CONFIRMED) {
        ESP_LOGE(TAG, "USB relay blocked: KVM timing is not confirmed");
        return ESP_ERR_INVALID_STATE;
    }
    if (duration_ms < 200 || duration_ms > 300) return ESP_ERR_INVALID_ARG;

    ESP_RETURN_ON_ERROR(gpio_set_level(APP_USB_RELAY_PIN, 1), TAG,
                        "USB relay close failed");
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    /* Always attempt release; this task is separate from UI rendering. */
    return gpio_set_level(APP_USB_RELAY_PIN, 0);
}

esp_err_t hardware_set_cooler_relay(bool closed)
{
    return gpio_set_level(APP_COOLER_RELAY_PIN, closed ? 1 : 0);
}
