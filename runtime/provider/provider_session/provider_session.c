#include "provider_session.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "provider_transport.h"
#include "provider_storage.h"

static const char *TAG = "provider_session";
static const char *TAILSCALE_OPENCLAW_HOST = "100.67.73.113";
static const char *LAN_OPENCLAW_HOST = "192.168.1.229";
static const uint16_t OPENCLAW_DEFAULT_PORT = 18789;

typedef struct {
    runtime_snapshot_t snapshot;
    provider_session_snapshot_cb_t listener;
    void *listener_ctx;
    bool initialized;
    bool has_saved_endpoint;
    char saved_endpoint_host[64];
    uint16_t saved_endpoint_port;
    int64_t last_watchdog_ms;
} provider_session_state_t;

static provider_session_state_t s_session;

static void notify_listener(void)
{
    if (s_session.listener != NULL) {
        s_session.listener(&s_session.snapshot, s_session.listener_ctx);
    }
}

static runtime_status_t map_transport_state(provider_transport_state_t state)
{
    switch (state) {
    case PROVIDER_TRANSPORT_STATE_CONNECTING:
        return RUNTIME_STATUS_CONNECTING;
    case PROVIDER_TRANSPORT_STATE_PAIRING:
        return s_session.snapshot.pair_code[0] != '\0'
                   ? RUNTIME_STATUS_PAIR_CODE_READY
                   : RUNTIME_STATUS_WAITING_FOR_APPROVAL;
    case PROVIDER_TRANSPORT_STATE_READY:
    case PROVIDER_TRANSPORT_STATE_BUSY:
        return RUNTIME_STATUS_READY;
    case PROVIDER_TRANSPORT_STATE_DISCONNECTED:
    default:
        return RUNTIME_STATUS_DISCONNECTED;
    }
}

static void set_detail(const char *detail)
{
    if (detail == 0) {
        s_session.snapshot.status_detail[0] = '\0';
        return;
    }

    strncpy(s_session.snapshot.status_detail, detail, sizeof(s_session.snapshot.status_detail) - 1);
    s_session.snapshot.status_detail[sizeof(s_session.snapshot.status_detail) - 1] = '\0';
}

static void set_detail_and_notify(const char *detail)
{
    set_detail(detail);
    notify_listener();
}

static void set_pair_code(const char *code)
{
    if (code == 0) {
        s_session.snapshot.pair_code[0] = '\0';
        return;
    }

    strncpy(s_session.snapshot.pair_code, code, sizeof(s_session.snapshot.pair_code) - 1);
    s_session.snapshot.pair_code[sizeof(s_session.snapshot.pair_code) - 1] = '\0';
}

static void refresh_token_flags(void)
{
    s_session.snapshot.has_gateway_token = provider_transport_has_gateway_token();
    s_session.snapshot.has_device_token = provider_transport_has_device_token();
}

static void set_saved_endpoint(const char *host, uint16_t port)
{
    s_session.has_saved_endpoint = host != NULL && host[0] != '\0' && port != 0;
    s_session.saved_endpoint_host[0] = '\0';
    s_session.saved_endpoint_port = 0;

    if (!s_session.has_saved_endpoint) {
        return;
    }

    strncpy(s_session.saved_endpoint_host, host, sizeof(s_session.saved_endpoint_host) - 1);
    s_session.saved_endpoint_host[sizeof(s_session.saved_endpoint_host) - 1] = '\0';
    s_session.saved_endpoint_port = port;
}

static void refresh_transport_snapshot_internal(bool notify)
{
    provider_transport_state_t transport_state;
    const char *detail;

    refresh_token_flags();
    transport_state = provider_transport_get_state();
    s_session.snapshot.status = map_transport_state(transport_state);

    if (transport_state == PROVIDER_TRANSPORT_STATE_DISCONNECTED &&
        s_session.snapshot.has_gateway_token &&
        !s_session.snapshot.has_device_token) {
        s_session.snapshot.status = RUNTIME_STATUS_PAIRING_REQUIRED;
    }

    detail = provider_transport_get_auth_stage();
    if (detail != NULL && detail[0] != '\0') {
        set_detail(detail);
    } else if (s_session.snapshot.status == RUNTIME_STATUS_PAIRING_REQUIRED) {
        set_detail("pairing_required");
    } else if (!s_session.snapshot.has_gateway_token && !s_session.snapshot.has_device_token) {
        set_detail("missing_runtime_token");
    } else {
        set_detail("disconnected");
    }

    if (notify) {
        notify_listener();
    }
}

static void refresh_transport_snapshot(void)
{
    refresh_transport_snapshot_internal(true);
}

static void session_on_pair_code(const char *code, void *ctx)
{
    (void)ctx;
    set_pair_code(code);
    refresh_transport_snapshot();
}

static void session_on_pair_done(void *ctx)
{
    (void)ctx;
    set_pair_code("");
    refresh_transport_snapshot();
}

static bool session_runtime_is_available(void)
{
    return s_session.initialized;
}

static bool session_runtime_get_snapshot(runtime_snapshot_t *out_snapshot)
{
    return provider_session_get_snapshot(out_snapshot);
}

static bool session_runtime_reconnect(void)
{
    return provider_session_reconnect();
}

static bool session_runtime_full_reset(void)
{
    return provider_session_full_reset();
}

bool provider_session_init(void)
{
    runtime_control_ops_t ops;
    char endpoint_host[64];
    uint16_t endpoint_port = 0;
    if (s_session.initialized) {
        return true;
    }

    memset(&s_session, 0, sizeof(s_session));
    if (!provider_transport_init(session_on_pair_code, session_on_pair_done, NULL)) {
        return false;
    }
    memset(endpoint_host, 0, sizeof(endpoint_host));
    if (provider_storage_load_endpoint_host(endpoint_host, sizeof(endpoint_host)) &&
        provider_storage_load_endpoint_port(&endpoint_port)) {
        if (strcmp(endpoint_host, TAILSCALE_OPENCLAW_HOST) == 0 && endpoint_port == OPENCLAW_DEFAULT_PORT) {
            ESP_LOGW(TAG,
                     "Restoring saved OpenClaw endpoint from %s:%u to %s:%u",
                     endpoint_host,
                     (unsigned)endpoint_port,
                     LAN_OPENCLAW_HOST,
                     (unsigned)OPENCLAW_DEFAULT_PORT);
            provider_storage_save_endpoint_host(LAN_OPENCLAW_HOST);
            provider_storage_save_endpoint_port(OPENCLAW_DEFAULT_PORT);
            strncpy(endpoint_host, LAN_OPENCLAW_HOST, sizeof(endpoint_host) - 1);
            endpoint_host[sizeof(endpoint_host) - 1] = '\0';
            endpoint_port = OPENCLAW_DEFAULT_PORT;
        }
        provider_transport_set_endpoint(endpoint_host, endpoint_port);
        set_saved_endpoint(endpoint_host, endpoint_port);
        ESP_LOGI(TAG, "Loaded saved OpenClaw endpoint %s:%u", endpoint_host, (unsigned)endpoint_port);
    }
    refresh_transport_snapshot();
    if (s_session.snapshot.status_detail[0] == '\0') {
        set_detail("initialized");
    }
    set_pair_code("");

    memset(&ops, 0, sizeof(ops));
    ops.is_available = session_runtime_is_available;
    ops.get_snapshot = session_runtime_get_snapshot;
    ops.reconnect = session_runtime_reconnect;
    ops.full_reset = session_runtime_full_reset;
    runtime_control_register_ops(&ops);

    s_session.initialized = true;
    ESP_LOGI(TAG,
             "Session init status=%d detail=%s gw=%d dev=%d host=%s port=%u",
             (int)s_session.snapshot.status,
             s_session.snapshot.status_detail,
             s_session.snapshot.has_gateway_token,
             s_session.snapshot.has_device_token,
             s_session.has_saved_endpoint ? s_session.saved_endpoint_host : "",
             (unsigned)(s_session.has_saved_endpoint ? s_session.saved_endpoint_port : 0));
    if (s_session.has_saved_endpoint &&
        (s_session.snapshot.has_gateway_token || s_session.snapshot.has_device_token)) {
        ESP_LOGI(TAG, "Auto-connecting OpenClaw from restored session state");
        provider_transport_disconnect();
        provider_transport_connect();
        refresh_transport_snapshot();
    }
    return true;
}

bool provider_session_get_snapshot(runtime_snapshot_t *out_snapshot)
{
    if (out_snapshot == 0 || !s_session.initialized) {
        return false;
    }

    refresh_transport_snapshot_internal(false);
    *out_snapshot = s_session.snapshot;
    return true;
}

bool provider_session_is_available(void)
{
    return s_session.initialized;
}

bool provider_session_register_listener(provider_session_snapshot_cb_t cb, void *ctx)
{
    if (!s_session.initialized) {
        return false;
    }

    s_session.listener = cb;
    s_session.listener_ctx = ctx;
    notify_listener();
    return true;
}

bool provider_session_save_gateway_token(const char *token)
{
    if (!s_session.initialized || token == 0) {
        return false;
    }
    ESP_LOGI(TAG, "Saving gateway token and reconnecting OpenClaw");
    provider_transport_set_gateway_token(token);
    set_pair_code("");
    provider_transport_disconnect();
    provider_transport_connect();
    refresh_transport_snapshot();
    return true;
}

bool provider_session_set_endpoint(const char *host, uint16_t port)
{
    bool ok;

    if (!s_session.initialized || host == NULL || host[0] == '\0' || port == 0) {
        return false;
    }

    ok = provider_transport_set_endpoint(host, port);
    ok = provider_storage_save_endpoint_host(host) && ok;
    ok = provider_storage_save_endpoint_port(port) && ok;
    if (!ok) {
        return false;
    }

    set_saved_endpoint(host, port);
    ESP_LOGI(TAG, "Saved OpenClaw endpoint %s:%u", host, (unsigned)port);
    refresh_transport_snapshot();
    return true;
}

bool provider_session_clear_endpoint(void)
{
    bool ok;

    if (!s_session.initialized) {
        return false;
    }

    provider_transport_disconnect();
    ok = provider_storage_clear_endpoint_host();
    ok = provider_storage_clear_endpoint_port() && ok;
    if (!ok) {
        return false;
    }

    set_saved_endpoint("", 0);
    refresh_transport_snapshot();
    set_detail_and_notify("endpoint_cleared");
    return true;
}

void provider_session_process(void)
{
    int64_t now_ms;
    int64_t watchdog_interval_ms;
    const char *auth_stage;

    if (!s_session.initialized) {
        return;
    }

    now_ms = (int64_t)(esp_timer_get_time() / 1000ULL);

    auth_stage = provider_transport_get_auth_stage();
    if (auth_stage == NULL) {
        auth_stage = "";
    }

    watchdog_interval_ms = strcmp(auth_stage, "waiting_time_sync") == 0 ? 1000LL : 5000LL;
    if ((now_ms - s_session.last_watchdog_ms) < watchdog_interval_ms) {
        return;
    }
    s_session.last_watchdog_ms = now_ms;

    if (s_session.has_saved_endpoint &&
        provider_transport_has_gateway_token() &&
        !provider_transport_is_connected() &&
        strcmp(auth_stage, "pairing_required") != 0 &&
        strcmp(auth_stage, "connecting_ws") != 0 &&
        strcmp(auth_stage, "ws_connected_waiting_challenge") != 0 &&
        strcmp(auth_stage, "challenge_received") != 0 &&
        strcmp(auth_stage, "connect_sent") != 0) {
        ESP_LOGW(TAG, "Watchdog reconnect auth_stage=%s", auth_stage);
        provider_transport_disconnect();
        provider_transport_connect();
        refresh_transport_snapshot();
        return;
    }

    if (s_session.has_saved_endpoint &&
        provider_transport_get_state() == PROVIDER_TRANSPORT_STATE_READY &&
        !provider_transport_is_connected()) {
        ESP_LOGW(TAG, "Watchdog zombie reconnect from READY without websocket");
        provider_transport_disconnect();
        provider_transport_connect();
        refresh_transport_snapshot();
        return;
    }

    if (s_session.has_saved_endpoint &&
        !provider_transport_has_device_token() &&
        provider_transport_has_gateway_token() &&
        !provider_transport_is_connected() &&
        strcmp(auth_stage, "pairing_required") == 0 &&
        provider_transport_get_chat_block_remaining_ms() == 0) {
        ESP_LOGW(TAG, "Watchdog retry after pairing approval window");
        provider_transport_disconnect();
        provider_transport_connect();
        refresh_transport_snapshot();
    }
}

bool provider_session_reconnect(void)
{
    if (!s_session.initialized) {
        return false;
    }

    set_pair_code("");
    refresh_transport_snapshot();
    if (!s_session.snapshot.has_gateway_token && !s_session.snapshot.has_device_token) {
        set_detail_and_notify("missing_runtime_token");
        return false;
    }
    if (!s_session.has_saved_endpoint) {
        set_detail_and_notify("missing_endpoint");
        return false;
    }

    provider_transport_disconnect();
    provider_transport_connect();
    refresh_transport_snapshot();
    return true;
}

bool provider_session_forget_device_token(void)
{
    if (!s_session.initialized) {
        return false;
    }

    set_pair_code("");
    provider_transport_forget_device_token();
    provider_transport_disconnect();
    refresh_transport_snapshot();
    if (s_session.snapshot.has_gateway_token) {
        s_session.snapshot.status = RUNTIME_STATUS_PAIRING_REQUIRED;
    }
    set_detail_and_notify("device_token_cleared");
    return true;
}

bool provider_session_full_reset(void)
{
    bool ok;

    if (!s_session.initialized) {
        return false;
    }

    provider_transport_disconnect();
    provider_transport_set_gateway_token("");
    provider_transport_forget_device_token();

    ok = provider_storage_clear_device_token();
    ok = provider_storage_clear_gateway_token() && ok;
    ok = provider_storage_clear_endpoint_host() && ok;
    ok = provider_storage_clear_endpoint_port() && ok;
    ok = provider_storage_clear_device_seed() && ok;
    if (!ok) {
        return false;
    }

    set_saved_endpoint("", 0);
    set_pair_code("");
    refresh_transport_snapshot();
    set_detail_and_notify("full_reset_complete");
    return true;
}

bool provider_session_start_pairing(void)
{
    if (!s_session.initialized) {
        return false;
    }

    set_pair_code("");
    if (!s_session.snapshot.has_gateway_token) {
        set_detail_and_notify("missing_gateway_token");
        return false;
    }

    provider_transport_start_pairing();
    refresh_transport_snapshot();
    return true;
}

bool provider_session_get_gateway_token(char *buf, size_t buf_len)
{
    if (!s_session.initialized || buf == 0 || buf_len == 0) {
        return false;
    }
    return provider_storage_load_gateway_token(buf, buf_len);
}
