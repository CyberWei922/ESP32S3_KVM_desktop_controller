#include <assert.h>
#include <stdio.h>

#include "button_gesture.h"

static button_gesture_output_t update(button_gesture_t *button, bool pressed, int64_t now_ms)
{
    return button_gesture_update(button, pressed, now_ms);
}

int main(void)
{
    button_gesture_t button;

    button_gesture_init(&button, false, 0);
    assert(update(&button, true, 10) == BUTTON_GESTURE_NONE);
    assert(update(&button, true, 39) == BUTTON_GESTURE_NONE);
    assert(update(&button, true, 40) == BUTTON_GESTURE_NONE);
    assert(update(&button, false, 100) == BUTTON_GESTURE_NONE);
    assert(update(&button, false, 130) == BUTTON_GESTURE_NONE);
    assert(update(&button, false, 429) == BUTTON_GESTURE_NONE);
    assert(update(&button, false, 430) == BUTTON_GESTURE_SINGLE);

    button_gesture_init(&button, false, 0);
    assert(update(&button, true, 10) == BUTTON_GESTURE_NONE);
    assert(update(&button, true, 40) == BUTTON_GESTURE_NONE);
    assert(update(&button, false, 80) == BUTTON_GESTURE_NONE);
    assert(update(&button, false, 110) == BUTTON_GESTURE_NONE);
    assert(update(&button, true, 170) == BUTTON_GESTURE_NONE);
    assert(update(&button, true, 200) == BUTTON_GESTURE_NONE);
    assert(update(&button, false, 240) == BUTTON_GESTURE_NONE);
    assert(update(&button, false, 270) == BUTTON_GESTURE_DOUBLE);
    assert(update(&button, false, 600) == BUTTON_GESTURE_NONE);

    button_gesture_init(&button, false, 0);
    assert(update(&button, true, 10) == BUTTON_GESTURE_NONE);
    assert(update(&button, true, 40) == BUTTON_GESTURE_NONE);
    assert(update(&button, true, 839) == BUTTON_GESTURE_NONE);
    assert(update(&button, true, 840) == BUTTON_GESTURE_LONG);
    assert(update(&button, false, 900) == BUTTON_GESTURE_NONE);
    assert(update(&button, false, 930) == BUTTON_GESTURE_NONE);

    button_gesture_init(&button, false, 0);
    assert(update(&button, true, 10) == BUTTON_GESTURE_NONE);
    assert(update(&button, false, 20) == BUTTON_GESTURE_NONE);
    assert(update(&button, true, 25) == BUTTON_GESTURE_NONE);
    assert(update(&button, false, 30) == BUTTON_GESTURE_NONE);
    assert(update(&button, false, 100) == BUTTON_GESTURE_NONE);

    puts("button gesture tests: PASS");
    return 0;
}

