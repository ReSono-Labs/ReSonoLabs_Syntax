#include "presentation.h"

presentation_state_t presentation_from_state(app_state_t state)
{
    presentation_state_t out = {
        .visual = PRESENTATION_VISUAL_IDLE,
        .status_text = "IDLE",
    };

    switch (state) {
    case APP_STATE_BOOT:
        out.visual = PRESENTATION_VISUAL_CONNECTING;
        out.status_text = "BOOTING";
        break;
    case APP_STATE_LOCKED:
        out.visual = PRESENTATION_VISUAL_LOCKED;
        out.status_text = "LOCKED";
        break;
    case APP_STATE_SETUP:
        out.visual = PRESENTATION_VISUAL_CONNECTING;
        out.status_text = "SETUP";
        break;
    case APP_STATE_CONNECTING:
        out.visual = PRESENTATION_VISUAL_CONNECTING;
        out.status_text = "CONNECTING";
        break;
    case APP_STATE_INITIALIZING:
        out.visual = PRESENTATION_VISUAL_CONNECTING;
        out.status_text = "INITIALIZING";
        break;
    case APP_STATE_IDLE:
        out.visual = PRESENTATION_VISUAL_IDLE;
        out.status_text = "READY";
        break;
    case APP_STATE_LISTENING:
        out.visual = PRESENTATION_VISUAL_LISTENING;
        out.status_text = "LISTENING";
        break;
    case APP_STATE_THINKING:
        out.visual = PRESENTATION_VISUAL_THINKING;
        out.status_text = "THINKING";
        break;
    case APP_STATE_TALKING:
        out.visual = PRESENTATION_VISUAL_TALKING;
        out.status_text = "TALKING";
        break;
    case APP_STATE_ERROR:
        out.visual = PRESENTATION_VISUAL_ERROR;
        out.status_text = "ERROR";
        break;
    case APP_STATE_SLEEP:
        out.visual = PRESENTATION_VISUAL_SLEEP;
        out.status_text = "SLEEP";
        break;
    default:
        break;
    }

    return out;
}
