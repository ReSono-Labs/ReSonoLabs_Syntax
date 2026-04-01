#ifndef PRESENTATION_H
#define PRESENTATION_H

#include "state.h"

typedef enum {
    PRESENTATION_VISUAL_IDLE = 0,
    PRESENTATION_VISUAL_CONNECTING,
    PRESENTATION_VISUAL_LISTENING,
    PRESENTATION_VISUAL_THINKING,
    PRESENTATION_VISUAL_TALKING,
    PRESENTATION_VISUAL_ERROR,
    PRESENTATION_VISUAL_SLEEP,
    PRESENTATION_VISUAL_LOCKED,
} presentation_visual_t;

typedef struct {
    presentation_visual_t visual;
    const char *status_text;
} presentation_state_t;

presentation_state_t presentation_from_state(app_state_t state);

#endif
