#include "provider_transport.h"

#include "provider_transport_donor.h"

typedef struct {
    provider_transport_pair_code_cb_t on_pair_code;
    provider_transport_pair_done_cb_t on_pair_done;
    void *ctx;
} provider_transport_callbacks_t;

static provider_transport_callbacks_t s_callbacks;
static provider_transport_terminal_event_cb_t s_terminal_cb;
static void *s_terminal_ctx;
static provider_transport_terminal_event_cb_t s_terminal_listener_cb;
static void *s_terminal_listener_ctx;

static void donor_on_pair_code(const char *code)
{
    if (s_callbacks.on_pair_code != 0) {
        s_callbacks.on_pair_code(code, s_callbacks.ctx);
    }
}

static void donor_on_pair_done(void)
{
    if (s_callbacks.on_pair_done != 0) {
        s_callbacks.on_pair_done(s_callbacks.ctx);
    }
}

static void donor_on_terminal_event(openclaw_terminal_event_type_t type,
                                    const uint8_t *data,
                                    size_t data_len,
                                    void *user_ctx)
{
    (void)user_ctx;

    if (s_terminal_listener_cb != 0) {
        s_terminal_listener_cb((provider_transport_terminal_event_type_t)type, data, data_len, s_terminal_listener_ctx);
    }

    if (s_terminal_cb != 0 && s_terminal_cb != s_terminal_listener_cb) {
        s_terminal_cb((provider_transport_terminal_event_type_t)type, data, data_len, s_terminal_ctx);
    }
}

bool provider_transport_init(provider_transport_pair_code_cb_t on_pair_code,
                             provider_transport_pair_done_cb_t on_pair_done,
                             void *ctx)
{
    s_callbacks.on_pair_code = on_pair_code;
    s_callbacks.on_pair_done = on_pair_done;
    s_callbacks.ctx = ctx;
    openclaw_init(donor_on_pair_code, donor_on_pair_done);
    return true;
}

bool provider_transport_set_endpoint(const char *host, uint16_t port)
{
    return openclaw_set_endpoint(host, port);
}

void provider_transport_connect(void)
{
    openclaw_connect();
}

void provider_transport_disconnect(void)
{
    openclaw_disconnect();
}

void provider_transport_start_pairing(void)
{
    openclaw_start_pairing();
}

provider_transport_state_t provider_transport_get_state(void)
{
    return (provider_transport_state_t)openclaw_get_state();
}

bool provider_transport_is_connected(void)
{
    return openclaw_is_ws_connected();
}

bool provider_transport_has_gateway_token(void)
{
    return openclaw_has_gateway_token();
}

bool provider_transport_has_device_token(void)
{
    return openclaw_has_token();
}

void provider_transport_forget_device_token(void)
{
    openclaw_forget_token();
}

void provider_transport_set_gateway_token(const char *token)
{
    openclaw_set_gateway_token(token);
}

const char *provider_transport_get_endpoint_host(void)
{
    return openclaw_get_endpoint_host();
}

uint16_t provider_transport_get_endpoint_port(void)
{
    return openclaw_get_endpoint_port();
}

const char *provider_transport_get_auth_stage(void)
{
    return openclaw_get_auth_stage();
}

int64_t provider_transport_get_chat_block_remaining_ms(void)
{
    return openclaw_get_chat_block_remaining_ms();
}

const char *provider_transport_get_chat_block_reason(void)
{
    return openclaw_get_chat_block_reason();
}

bool provider_transport_terminal_open(provider_transport_terminal_event_cb_t cb, void *ctx)
{
    s_terminal_cb = cb;
    s_terminal_ctx = ctx;
    return openclaw_terminal_open(donor_on_terminal_event, ctx);
}

bool provider_transport_terminal_register_listener(provider_transport_terminal_event_cb_t cb, void *ctx)
{
    s_terminal_listener_cb = cb;
    s_terminal_listener_ctx = ctx;
    return openclaw_terminal_register_listener(donor_on_terminal_event, ctx);
}

bool provider_transport_terminal_send_audio(const uint8_t *audio_data, size_t audio_len)
{
    return openclaw_terminal_send_audio(audio_data, audio_len);
}

bool provider_transport_terminal_send_text(const char *text)
{
    return openclaw_terminal_send_text(text);
}

bool provider_transport_terminal_activity_start(void)
{
    return openclaw_terminal_activity_start();
}

bool provider_transport_terminal_activity_end(void)
{
    return openclaw_terminal_activity_end();
}

bool provider_transport_results_replay(const char *task_id)
{
    return openclaw_results_replay(task_id);
}

bool provider_transport_results_consume(const char *task_id)
{
    return openclaw_results_consume(task_id);
}

void provider_transport_terminal_close(void)
{
    openclaw_terminal_close();
    s_terminal_cb = 0;
    s_terminal_ctx = 0;
}

bool provider_transport_terminal_is_active(void)
{
    return openclaw_terminal_is_active();
}
