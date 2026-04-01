#ifndef INPUT_PLATFORM_H
#define INPUT_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#include "board_profile.h"

typedef enum {
    INPUT_EVENT_TAP = 0,
    INPUT_EVENT_PRESS,
    INPUT_EVENT_RELEASE,
    INPUT_EVENT_GESTURE_SWIPE_DOWN,
    INPUT_EVENT_GESTURE_SWIPE_UP,
    INPUT_EVENT_BUTTON_SHORT_PRESS,
    INPUT_EVENT_BUTTON_LONG_PRESS,
} input_event_type_t;

typedef struct {
    input_event_type_t type;
    int16_t x;
    int16_t y;
    bool has_coordinates;
} input_event_t;

typedef void (*input_event_cb_t)(const input_event_t *event, void *ctx);

bool input_platform_init(const board_profile_t *board);
void input_platform_register_callback(input_event_cb_t cb, void *ctx);
bool input_platform_get_pointer_state(bool *touched, uint16_t *x, uint16_t *y);
void input_platform_process(void);

#endif
