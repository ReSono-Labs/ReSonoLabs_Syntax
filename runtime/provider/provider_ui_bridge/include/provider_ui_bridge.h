#ifndef PROVIDER_UI_BRIDGE_H
#define PROVIDER_UI_BRIDGE_H

#include <stdbool.h>

#include "provider_terminal.h"
#include "runtime_control.h"

typedef struct {
    bool available;
    bool ready;
    bool mic_active;
    bool turn_in_progress;
    bool session_open;
    runtime_status_t runtime_status;
    char status_line[64];
    char status_detail[64];
    char pair_code[64];
    char terminal_text[128];
    char terminal_error[96];
} provider_ui_view_t;

bool provider_ui_bridge_get_view(provider_ui_view_t *out_view);

#endif
