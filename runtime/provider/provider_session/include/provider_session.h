#ifndef PROVIDER_SESSION_H
#define PROVIDER_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "runtime_control.h"

typedef void (*provider_session_snapshot_cb_t)(const runtime_snapshot_t *snapshot, void *ctx);

bool provider_session_init(void);
bool provider_session_get_snapshot(runtime_snapshot_t *out_snapshot);
bool provider_session_is_available(void);
bool provider_session_register_listener(provider_session_snapshot_cb_t cb, void *ctx);

bool provider_session_save_gateway_token(const char *token);
bool provider_session_set_endpoint(const char *host, uint16_t port);
bool provider_session_clear_endpoint(void);
void provider_session_process(void);
bool provider_session_reconnect(void);
bool provider_session_forget_device_token(void);
bool provider_session_full_reset(void);
bool provider_session_start_pairing(void);

bool provider_session_get_gateway_token(char *buf, size_t buf_len);

#endif
