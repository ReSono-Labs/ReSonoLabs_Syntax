#include "state.h"

#define APP_STATE_CB_MAX 8

typedef struct {
    app_state_change_cb_t cb;
    void *ctx;
} app_state_callback_slot_t;

static app_state_t s_state = APP_STATE_BOOT;
static app_state_callback_slot_t s_callbacks[APP_STATE_CB_MAX];

void app_state_init(void)
{
    s_state = APP_STATE_BOOT;
}

app_state_t app_state_get(void)
{
    return s_state;
}

void app_state_set(app_state_t state)
{
    int i;

    s_state = state;
    for (i = 0; i < APP_STATE_CB_MAX; ++i) {
        if (s_callbacks[i].cb != 0) {
            s_callbacks[i].cb(state, s_callbacks[i].ctx);
        }
    }
}

void app_state_register_callback(app_state_change_cb_t cb, void *ctx)
{
    int i;

    for (i = 0; i < APP_STATE_CB_MAX; ++i) {
        if (s_callbacks[i].cb == 0) {
            s_callbacks[i].cb = cb;
            s_callbacks[i].ctx = ctx;
            return;
        }
    }
}
