#ifndef PROVIDER_TRANSPORT_H
#define PROVIDER_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PROVIDER_TRANSPORT_STATE_DISCONNECTED = 0,
    PROVIDER_TRANSPORT_STATE_CONNECTING,
    PROVIDER_TRANSPORT_STATE_PAIRING,
    PROVIDER_TRANSPORT_STATE_READY,
    PROVIDER_TRANSPORT_STATE_BUSY,
} provider_transport_state_t;

typedef enum {
    PROVIDER_TRANSPORT_TERMINAL_EVENT_READY = 0,
    PROVIDER_TRANSPORT_TERMINAL_EVENT_TEXT,
    PROVIDER_TRANSPORT_TERMINAL_EVENT_AUDIO,
    PROVIDER_TRANSPORT_TERMINAL_EVENT_TURN_COMPLETE,
    PROVIDER_TRANSPORT_TERMINAL_EVENT_ERROR,
    PROVIDER_TRANSPORT_TERMINAL_EVENT_NOTIFICATION_READY,
    PROVIDER_TRANSPORT_TERMINAL_EVENT_NOTIFICATION_CLEAR,
    PROVIDER_TRANSPORT_TERMINAL_EVENT_NOTIFICATION_REPLAY,
} provider_transport_terminal_event_type_t;

typedef void (*provider_transport_pair_code_cb_t)(const char *code, void *ctx);
typedef void (*provider_transport_pair_done_cb_t)(void *ctx);
typedef void (*provider_transport_terminal_event_cb_t)(provider_transport_terminal_event_type_t type,
                                                       const uint8_t *data,
                                                       size_t data_len,
                                                       void *ctx);

bool provider_transport_init(provider_transport_pair_code_cb_t on_pair_code,
                             provider_transport_pair_done_cb_t on_pair_done,
                             void *ctx);
bool provider_transport_set_endpoint(const char *host, uint16_t port);
void provider_transport_connect(void);
void provider_transport_disconnect(void);
void provider_transport_start_pairing(void);
provider_transport_state_t provider_transport_get_state(void);
bool provider_transport_is_connected(void);
bool provider_transport_has_gateway_token(void);
bool provider_transport_has_device_token(void);
void provider_transport_forget_device_token(void);
void provider_transport_set_gateway_token(const char *token);
const char *provider_transport_get_endpoint_host(void);
uint16_t provider_transport_get_endpoint_port(void);
const char *provider_transport_get_auth_stage(void);
int64_t provider_transport_get_chat_block_remaining_ms(void);
const char *provider_transport_get_chat_block_reason(void);

bool provider_transport_terminal_open(provider_transport_terminal_event_cb_t cb, void *ctx);
bool provider_transport_terminal_register_listener(provider_transport_terminal_event_cb_t cb, void *ctx);
bool provider_transport_terminal_send_audio(const uint8_t *audio_data, size_t audio_len);
bool provider_transport_terminal_send_text(const char *text);
bool provider_transport_terminal_activity_start(void);
bool provider_transport_terminal_activity_end(void);
bool provider_transport_results_replay(const char *task_id);
bool provider_transport_results_consume(const char *task_id);
void provider_transport_terminal_close(void);
bool provider_transport_terminal_is_active(void);

#endif
