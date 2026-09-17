#include "button_gesture.h"

#include <string.h>

#include "interaction_config.h"

void button_gesture_init(button_gesture_t *gesture, bool pressed, int64_t now_ms)
{
    if (gesture == NULL) return;
    memset(gesture, 0, sizeof(*gesture));
    gesture->candidate_pressed = pressed;
    gesture->stable_pressed = pressed;
    gesture->candidate_since_ms = now_ms;
    gesture->pressed_since_ms = now_ms;
}

button_gesture_output_t button_gesture_update(button_gesture_t *gesture, bool pressed,
                                              int64_t now_ms)
{
    if (gesture == NULL) return BUTTON_GESTURE_NONE;
    button_gesture_output_t output = BUTTON_GESTURE_NONE;

    /* Finalize a waiting click before accepting a late second press. */
    if (!gesture->stable_pressed && gesture->pending_clicks == 1 &&
        now_ms >= gesture->click_deadline_ms) {
        gesture->pending_clicks = 0;
        output = BUTTON_GESTURE_SINGLE;
    }

    if (pressed != gesture->candidate_pressed) {
        gesture->candidate_pressed = pressed;
        gesture->candidate_since_ms = now_ms;
    } else if (pressed != gesture->stable_pressed &&
               now_ms - gesture->candidate_since_ms >= APP_BUTTON_DEBOUNCE_MS) {
        gesture->stable_pressed = pressed;
        if (pressed) {
            gesture->pressed_since_ms = now_ms;
            gesture->long_emitted = false;
            gesture->second_press = gesture->pending_clicks == 1;
        } else if (gesture->long_emitted) {
            gesture->second_press = false;
        } else if (gesture->second_press) {
            gesture->pending_clicks = 0;
            gesture->second_press = false;
            output = BUTTON_GESTURE_DOUBLE;
        } else {
            gesture->pending_clicks = 1;
            gesture->click_deadline_ms = now_ms + APP_BUTTON_DOUBLE_CLICK_MS;
        }
    }

    if (gesture->stable_pressed && !gesture->long_emitted &&
        now_ms - gesture->pressed_since_ms >= APP_BUTTON_LONG_PRESS_MS) {
        gesture->long_emitted = true;
        gesture->pending_clicks = 0;
        gesture->second_press = false;
        output = BUTTON_GESTURE_LONG;
    }
    return output;
}

button_gesture_output_t button_gesture_update_right(button_gesture_t *gesture, bool pressed,
                                                    int64_t now_ms)
{
    if (gesture == NULL) return BUTTON_GESTURE_NONE;
    button_gesture_output_t output = BUTTON_GESTURE_NONE;

    if (!gesture->stable_pressed && gesture->pending_clicks == 1 &&
        now_ms >= gesture->click_deadline_ms) {
        gesture->pending_clicks = 0;
        output = BUTTON_GESTURE_SINGLE;
    }

    if (pressed != gesture->candidate_pressed) {
        gesture->candidate_pressed = pressed;
        gesture->candidate_since_ms = now_ms;
    } else if (pressed != gesture->stable_pressed &&
               now_ms - gesture->candidate_since_ms >= APP_BUTTON_DEBOUNCE_MS) {
        gesture->stable_pressed = pressed;
        if (pressed) {
            gesture->pressed_since_ms = now_ms;
            gesture->long_emitted = false;
            gesture->second_press = gesture->pending_clicks == 1;
        } else if (gesture->long_emitted) {
            gesture->second_press = false;
        } else if (now_ms - gesture->pressed_since_ms >= APP_BUTTON_RIGHT_MODE_MS) {
            gesture->pending_clicks = 0;
            gesture->second_press = false;
            output = BUTTON_GESTURE_MEDIUM_RELEASE;
        } else if (gesture->second_press) {
            gesture->pending_clicks = 0;
            gesture->second_press = false;
            output = BUTTON_GESTURE_DOUBLE;
        } else {
            gesture->pending_clicks = 1;
            gesture->click_deadline_ms = now_ms + APP_BUTTON_DOUBLE_CLICK_MS;
        }
    }

    if (gesture->stable_pressed && !gesture->long_emitted &&
        now_ms - gesture->pressed_since_ms >= APP_BUTTON_RIGHT_POWER_MS) {
        gesture->long_emitted = true;
        gesture->pending_clicks = 0;
        gesture->second_press = false;
        output = BUTTON_GESTURE_VERY_LONG;
    }
    return output;
}
