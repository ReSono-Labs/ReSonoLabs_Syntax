#ifndef PROVIDER_TERMINAL_H
#define PROVIDER_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "provider_transport.h"

typedef struct {
    bool session_open;
    bool ready;
    bool mic_active;
    bool turn_in_progress;
    size_t last_audio_bytes;
    char last_text[256];
    char last_error[160];
} provider_terminal_snapshot_t;

typedef void (*provider_terminal_event_cb_t)(provider_transport_terminal_event_type_t type,
                                             const uint8_t *data,
                                             size_t data_len,
                                             void *ctx);

bool provider_terminal_init(void);
bool provider_terminal_get_snapshot(provider_terminal_snapshot_t *out_snapshot);
bool provider_terminal_open(void);
void provider_terminal_close(void);
bool provider_terminal_start_mic(void);
bool provider_terminal_stop_mic(void);
bool provider_terminal_send_audio(const uint8_t *audio_data, size_t audio_len);
bool provider_terminal_send_text(const char *text);
bool provider_terminal_register_listener(provider_terminal_event_cb_t cb, void *ctx);
bool provider_terminal_results_replay(const char *task_id);
bool provider_terminal_results_consume(const char *task_id);

#endif
