#ifndef STATE_H
#define STATE_H

typedef enum {
    APP_STATE_BOOT = 0,
    APP_STATE_LOCKED,
    APP_STATE_SETUP,
    APP_STATE_CONNECTING,
    APP_STATE_INITIALIZING,
    APP_STATE_IDLE,
    APP_STATE_LISTENING,
    APP_STATE_THINKING,
    APP_STATE_TALKING,
    APP_STATE_ERROR,
    APP_STATE_SLEEP,
} app_state_t;

typedef void (*app_state_change_cb_t)(app_state_t state, void *ctx);

void app_state_init(void);
app_state_t app_state_get(void);
void app_state_set(app_state_t state);
void app_state_register_callback(app_state_change_cb_t cb, void *ctx);

#endif
