#include "provider_runtime_api.h"

#include "provider_session.h"

bool provider_runtime_init(void)
{
    return provider_session_init() && provider_terminal_init();
}

bool provider_runtime_get_snapshot(runtime_snapshot_t *out_snapshot)
{
    return provider_session_get_snapshot(out_snapshot);
}

bool provider_runtime_save_gateway_token(const char *token)
{
    return provider_session_save_gateway_token(token);
}

bool provider_runtime_set_endpoint(const char *host, uint16_t port)
{
    return provider_session_set_endpoint(host, port);
}

bool provider_runtime_clear_endpoint(void)
{
    return provider_session_clear_endpoint();
}

void provider_runtime_process(void)
{
    provider_session_process();
}

bool provider_runtime_reconnect(void)
{
    return provider_session_reconnect();
}

bool provider_runtime_forget_device_token(void)
{
    return provider_session_forget_device_token();
}

bool provider_runtime_full_reset(void)
{
    return provider_session_full_reset();
}

bool provider_runtime_start_pairing(void)
{
    return provider_session_start_pairing();
}

bool provider_runtime_terminal_get_snapshot(provider_terminal_snapshot_t *out_snapshot)
{
    return provider_terminal_get_snapshot(out_snapshot);
}

bool provider_runtime_terminal_open(void)
{
    return provider_terminal_open();
}

void provider_runtime_terminal_close(void)
{
    provider_terminal_close();
}

bool provider_runtime_terminal_start_mic(void)
{
    return provider_terminal_start_mic();
}

bool provider_runtime_terminal_stop_mic(void)
{
    return provider_terminal_stop_mic();
}

bool provider_runtime_terminal_send_audio(const uint8_t *audio_data, size_t audio_len)
{
    return provider_terminal_send_audio(audio_data, audio_len);
}

bool provider_runtime_terminal_send_text(const char *text)
{
    return provider_terminal_send_text(text);
}

bool provider_runtime_terminal_register_listener(provider_terminal_event_cb_t cb, void *ctx)
{
    return provider_terminal_register_listener(cb, ctx);
}

bool provider_runtime_results_replay(const char *task_id)
{
    return provider_terminal_results_replay(task_id);
}

bool provider_runtime_results_consume(const char *task_id)
{
    return provider_terminal_results_consume(task_id);
}

bool provider_runtime_get_ui_view(provider_ui_view_t *out_view)
{
    return provider_ui_bridge_get_view(out_view);
}
