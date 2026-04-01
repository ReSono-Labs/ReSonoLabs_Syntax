#ifndef PROVIDER_RUNTIME_API_H
#define PROVIDER_RUNTIME_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "provider_ui_bridge.h"
#include "provider_terminal.h"
#include "runtime_control.h"

bool provider_runtime_init(void);
bool provider_runtime_get_snapshot(runtime_snapshot_t *out_snapshot);
bool provider_runtime_save_gateway_token(const char *token);
bool provider_runtime_set_endpoint(const char *host, uint16_t port);
bool provider_runtime_clear_endpoint(void);
void provider_runtime_process(void);
bool provider_runtime_reconnect(void);
bool provider_runtime_forget_device_token(void);
bool provider_runtime_full_reset(void);
bool provider_runtime_start_pairing(void);
bool provider_runtime_terminal_get_snapshot(provider_terminal_snapshot_t *out_snapshot);
bool provider_runtime_terminal_open(void);
void provider_runtime_terminal_close(void);
bool provider_runtime_terminal_start_mic(void);
bool provider_runtime_terminal_stop_mic(void);
bool provider_runtime_terminal_send_audio(const uint8_t *audio_data, size_t audio_len);
bool provider_runtime_terminal_send_text(const char *text);
bool provider_runtime_terminal_register_listener(provider_terminal_event_cb_t cb, void *ctx);
bool provider_runtime_results_replay(const char *task_id);
bool provider_runtime_results_consume(const char *task_id);
bool provider_runtime_get_ui_view(provider_ui_view_t *out_view);

#endif
