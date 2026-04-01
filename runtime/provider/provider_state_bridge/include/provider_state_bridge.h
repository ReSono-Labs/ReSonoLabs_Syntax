#ifndef PROVIDER_STATE_BRIDGE_H
#define PROVIDER_STATE_BRIDGE_H

#include <stdbool.h>

bool provider_state_bridge_init(void);
void provider_state_bridge_sync(void);
bool provider_state_bridge_handle_idle_tap(void);
bool provider_state_bridge_handle_listening_tap(void);
void provider_state_bridge_handle_error_tap(void);

#endif
