#include "buttons.h"

#include "driver/gpio.h"

#define BUTTON_DEBOUNCE_MS 30

typedef struct {
    gpio_num_t pin;
    uint8_t mask;
    bool candidate_pressed;
    bool stable_pressed;
    int64_t candidate_since_ms;
} button_state_t;

static button_state_t s_buttons[] = {
    {GPIO_NUM_4, BUTTON_MASK_LEFT, false, false, 0},
    {GPIO_NUM_5, BUTTON_MASK_MIDDLE, false, false, 0},
    {GPIO_NUM_6, BUTTON_MASK_RIGHT, false, false, 0},
};

static bool read_pressed(gpio_num_t pin)
{
    return gpio_get_level(pin) == 0;
}

esp_err_t buttons_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (UINT64_C(1) << BUTTON_PIN_LEFT) |
                        (UINT64_C(1) << BUTTON_PIN_MIDDLE) |
                        (UINT64_C(1) << BUTTON_PIN_RIGHT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t result = gpio_config(&config);
    if (result != ESP_OK) return result;

    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
        bool pressed = read_pressed(s_buttons[i].pin);
        s_buttons[i].candidate_pressed = pressed;
        s_buttons[i].stable_pressed = pressed;
    }
    return ESP_OK;
}

bool buttons_poll(int64_t now_ms, uint8_t *pressed_mask)
{
    bool changed = false;
    uint8_t mask = 0;

    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
        button_state_t *button = &s_buttons[i];
        bool pressed = read_pressed(button->pin);

        if (pressed != button->candidate_pressed) {
            button->candidate_pressed = pressed;
            button->candidate_since_ms = now_ms;
        } else if (pressed != button->stable_pressed &&
                   now_ms - button->candidate_since_ms >= BUTTON_DEBOUNCE_MS) {
            button->stable_pressed = pressed;
            changed = true;
        }

        if (button->stable_pressed) mask |= button->mask;
    }

    if (pressed_mask != NULL) *pressed_mask = mask;
    return changed;
}
