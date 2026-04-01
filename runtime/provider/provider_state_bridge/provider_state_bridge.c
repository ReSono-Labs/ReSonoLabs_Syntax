#include "provider_state_bridge.h"

#include <string.h>

#include "esp_log.h"
#include "provider_session.h"
#include "provider_runtime_api.h"
#include "state.h"

static const char *TAG = "provider_state";

typedef struct {
    bool initialized;
} provider_state_bridge_state_t;

static provider_state_bridge_state_t s_bridge;

static void apply_runtime_snapshot(const runtime_snapshot_t *runtime);

static void on_session_snapshot(const runtime_snapshot_t *runtime, void *ctx)
{
    (void)ctx;
    apply_runtime_snapshot(runtime);
}

static void apply_runtime_snapshot(const runtime_snapshot_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    switch (runtime->status) {
    case RUNTIME_STATUS_CONNECTING:
    case RUNTIME_STATUS_PAIRING_REQUIRED:
    case RUNTIME_STATUS_PAIR_CODE_READY:
    case RUNTIME_STATUS_WAITING_FOR_APPROVAL:
        app_state_set(APP_STATE_CONNECTING);
        break;
    case RUNTIME_STATUS_READY:
        if (app_state_get() == APP_STATE_BOOT ||
            app_state_get() == APP_STATE_CONNECTING ||
            app_state_get() == APP_STATE_ERROR) {
            app_state_set(APP_STATE_IDLE);
        }
        break;
    case RUNTIME_STATUS_ERROR:
        app_state_set(APP_STATE_ERROR);
        break;
    case RUNTIME_STATUS_DISCONNECTED:
    default:
        if (app_state_get() == APP_STATE_CONNECTING) {
            app_state_set(APP_STATE_IDLE);
        }
        break;
    }
}

bool provider_state_bridge_init(void)
{
    if (s_bridge.initialized) {
        return true;
    }

    memset(&s_bridge, 0, sizeof(s_bridge));
    if (!provider_session_register_listener(on_session_snapshot, NULL)) {
        return false;
    }

    provider_state_bridge_sync();
    s_bridge.initialized = true;
    return true;
}

void provider_state_bridge_sync(void)
{
    runtime_snapshot_t runtime;

    if (!runtime_control_is_available()) {
        return;
    }

    memset(&runtime, 0, sizeof(runtime));

    if (runtime_control_get_snapshot(&runtime)) {
        apply_runtime_snapshot(&runtime);
    }
}

bool provider_state_bridge_handle_idle_tap(void)
{
    runtime_snapshot_t runtime;
    provider_terminal_snapshot_t terminal;

    if (!runtime_control_is_available()) {
        return false;
    }

    memset(&runtime, 0, sizeof(runtime));
    if (!provider_runtime_get_snapshot(&runtime) ||
        runtime.status != RUNTIME_STATUS_READY ||
        !runtime.has_device_token) {
        ESP_LOGW(TAG, "Idle tap ignored: runtime not paired-ready");
        return false;
    }

    memset(&terminal, 0, sizeof(terminal));
    if (!provider_runtime_terminal_get_snapshot(&terminal)) {
        ESP_LOGW(TAG, "Idle tap ignored: terminal snapshot unavailable");
        return false;
    }

    ESP_LOGI(TAG, "Idle tap session_open=%d mic_active=%d turn_in_progress=%d",
             terminal.session_open,
             terminal.mic_active,
             terminal.turn_in_progress);
    if (!terminal.session_open && !provider_runtime_terminal_open()) {
        ESP_LOGW(TAG, "Idle tap failed: terminal open failed");
        return false;
    }
    if (!provider_runtime_terminal_start_mic()) {
        ESP_LOGW(TAG, "Idle tap failed: mic start failed");
        return false;
    }

    app_state_set(APP_STATE_LISTENING);
    ESP_LOGI(TAG, "Idle tap -> LISTENING");
    return true;
}

bool provider_state_bridge_handle_listening_tap(void)
{
    if (!runtime_control_is_available()) {
        ESP_LOGW(TAG, "Listening tap ignored: runtime unavailable");
        return false;
    }
    if (!provider_runtime_terminal_stop_mic()) {
        ESP_LOGW(TAG, "Listening tap failed: mic stop failed");
        return false;
    }

    app_state_set(APP_STATE_THINKING);
    ESP_LOGI(TAG, "Listening tap -> THINKING");
    return true;
}

void provider_state_bridge_handle_error_tap(void)
{
    provider_state_bridge_sync();
}
