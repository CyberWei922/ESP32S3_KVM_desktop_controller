#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BUTTON_GESTURE_NONE = 0,
    BUTTON_GESTURE_SINGLE,
    BUTTON_GESTURE_DOUBLE,
    BUTTON_GESTURE_LONG,
    BUTTON_GESTURE_MEDIUM_RELEASE,
    BUTTON_GESTURE_VERY_LONG,
} button_gesture_output_t;

typedef struct {
    bool candidate_pressed;
    bool stable_pressed;
    bool long_emitted;
    bool second_press;
    uint8_t pending_clicks;
    int64_t candidate_since_ms;
    int64_t pressed_since_ms;
    int64_t click_deadline_ms;
} button_gesture_t;

void button_gesture_init(button_gesture_t *gesture, bool pressed, int64_t now_ms);
button_gesture_output_t button_gesture_update(button_gesture_t *gesture, bool pressed,
                                              int64_t now_ms);
button_gesture_output_t button_gesture_update_right(button_gesture_t *gesture, bool pressed,
                                                    int64_t now_ms);
