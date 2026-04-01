#include "provider_terminal.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "provider_terminal";

typedef struct {
    provider_terminal_event_cb_t cb;
    void *ctx;
} provider_terminal_listener_t;

#define PROVIDER_TERMINAL_MAX_LISTENERS 4

typedef struct {
    provider_terminal_snapshot_t snapshot;
    provider_terminal_listener_t listeners[PROVIDER_TERMINAL_MAX_LISTENERS];
    bool initialized;
} provider_terminal_state_t;

static provider_terminal_state_t s_terminal;

static void set_text(char *dst, size_t dst_len, const uint8_t *src, size_t src_len)
{
    size_t copy_len;

    if (dst == NULL || dst_len == 0) {
        return;
    }

    dst[0] = '\0';
    if (src == NULL || src_len == 0) {
        return;
    }

    copy_len = src_len;
    if (copy_len >= dst_len) {
        copy_len = dst_len - 1;
    }

    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

static void terminal_on_event(provider_transport_terminal_event_type_t type,
                              const uint8_t *data,
                              size_t data_len,
                              void *ctx)
{
    (void)ctx;

    switch (type) {
    case PROVIDER_TRANSPORT_TERMINAL_EVENT_READY:
        s_terminal.snapshot.session_open = true;
        s_terminal.snapshot.ready = true;
        s_terminal.snapshot.turn_in_progress = false;
        s_terminal.snapshot.last_error[0] = '\0';
        break;
    case PROVIDER_TRANSPORT_TERMINAL_EVENT_TEXT:
        set_text(s_terminal.snapshot.last_text, sizeof(s_terminal.snapshot.last_text), data, data_len);
        break;
    case PROVIDER_TRANSPORT_TERMINAL_EVENT_AUDIO:
        s_terminal.snapshot.last_audio_bytes = data_len;
        break;
    case PROVIDER_TRANSPORT_TERMINAL_EVENT_TURN_COMPLETE:
        s_terminal.snapshot.turn_in_progress = false;
        s_terminal.snapshot.mic_active = false;
        break;
    case PROVIDER_TRANSPORT_TERMINAL_EVENT_ERROR:
        set_text(s_terminal.snapshot.last_error, sizeof(s_terminal.snapshot.last_error), data, data_len);
        s_terminal.snapshot.turn_in_progress = false;
        s_terminal.snapshot.mic_active = false;
        break;
    default:
        break;
    }

    for (size_t i = 0; i < PROVIDER_TERMINAL_MAX_LISTENERS; ++i) {
        if (s_terminal.listeners[i].cb != NULL) {
            s_terminal.listeners[i].cb(type, data, data_len, s_terminal.listeners[i].ctx);
        }
    }
}

bool provider_terminal_init(void)
{
    if (s_terminal.initialized) {
        return true;
    }

    memset(&s_terminal, 0, sizeof(s_terminal));
    if (!provider_transport_terminal_register_listener(terminal_on_event, NULL)) {
        ESP_LOGW(TAG, "Persistent terminal listener registration failed");
        return false;
    }
    s_terminal.initialized = true;
    return true;
}

bool provider_terminal_get_snapshot(provider_terminal_snapshot_t *out_snapshot)
{
    if (!s_terminal.initialized || out_snapshot == NULL) {
        return false;
    }

    s_terminal.snapshot.session_open = provider_transport_terminal_is_active();
    *out_snapshot = s_terminal.snapshot;
    return true;
}

bool provider_terminal_open(void)
{
    if (!s_terminal.initialized) {
        return false;
    }

    s_terminal.snapshot.last_text[0] = '\0';
    s_terminal.snapshot.last_error[0] = '\0';
    s_terminal.snapshot.last_audio_bytes = 0;
    s_terminal.snapshot.turn_in_progress = false;
    s_terminal.snapshot.mic_active = false;

    if (!provider_transport_terminal_open(terminal_on_event, NULL)) {
        ESP_LOGW(TAG, "Terminal open failed");
        return false;
    }

    s_terminal.snapshot.session_open = true;
    return true;
}

void provider_terminal_close(void)
{
    if (!s_terminal.initialized) {
        return;
    }

    provider_transport_terminal_close();
    s_terminal.snapshot.session_open = false;
    s_terminal.snapshot.ready = false;
    s_terminal.snapshot.mic_active = false;
    s_terminal.snapshot.turn_in_progress = false;
}

bool provider_terminal_start_mic(void)
{
    if (!s_terminal.initialized || !s_terminal.snapshot.session_open) {
        ESP_LOGW(TAG, "Start mic rejected initialized=%d session_open=%d",
                 s_terminal.initialized,
                 s_terminal.snapshot.session_open);
        return false;
    }
    if (!provider_transport_terminal_activity_start()) {
        ESP_LOGW(TAG, "Transport rejected mic start");
        return false;
    }

    s_terminal.snapshot.mic_active = true;
    s_terminal.snapshot.turn_in_progress = true;
    return true;
}

bool provider_terminal_stop_mic(void)
{
    if (!s_terminal.initialized || !s_terminal.snapshot.session_open) {
        ESP_LOGW(TAG, "Stop mic rejected initialized=%d session_open=%d",
                 s_terminal.initialized,
                 s_terminal.snapshot.session_open);
        return false;
    }
    if (!provider_transport_terminal_activity_end()) {
        ESP_LOGW(TAG, "Transport rejected mic stop");
        return false;
    }

    s_terminal.snapshot.mic_active = false;
    return true;
}

bool provider_terminal_send_audio(const uint8_t *audio_data, size_t audio_len)
{
    if (!s_terminal.initialized || !s_terminal.snapshot.session_open) {
        return false;
    }
    if (!provider_transport_terminal_send_audio(audio_data, audio_len)) {
        return false;
    }

    s_terminal.snapshot.turn_in_progress = true;
    return true;
}

bool provider_terminal_send_text(const char *text)
{
    if (!s_terminal.initialized || !s_terminal.snapshot.session_open) {
        return false;
    }
    if (!provider_transport_terminal_send_text(text)) {
        return false;
    }

    s_terminal.snapshot.turn_in_progress = true;
    return true;
}

bool provider_terminal_register_listener(provider_terminal_event_cb_t cb, void *ctx)
{
    if (!s_terminal.initialized) {
        return false;
    }

    for (size_t i = 0; i < PROVIDER_TERMINAL_MAX_LISTENERS; ++i) {
        if (s_terminal.listeners[i].cb == cb && s_terminal.listeners[i].ctx == ctx) {
            return true;
        }
    }

    for (size_t i = 0; i < PROVIDER_TERMINAL_MAX_LISTENERS; ++i) {
        if (s_terminal.listeners[i].cb == NULL) {
            s_terminal.listeners[i].cb = cb;
            s_terminal.listeners[i].ctx = ctx;
            return true;
        }
    }

    ESP_LOGW(TAG, "Terminal listener registration full");
    return false;
}

bool provider_terminal_results_replay(const char *task_id)
{
    return provider_transport_results_replay(task_id);
}

bool provider_terminal_results_consume(const char *task_id)
{
    return provider_transport_results_consume(task_id);
}
