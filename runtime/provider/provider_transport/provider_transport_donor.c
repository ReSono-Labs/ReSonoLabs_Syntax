#include "provider_transport_donor.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/time.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "mbedtls/base64.h"
#include "mbedtls/md.h"
#include "compact_ed25519.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "openclaw";

#define NVS_NAMESPACE        "provider_rt"
#define NVS_KEY_TOKEN        "device_token"
#define NVS_KEY_GW_TOKEN     "gateway_token"
#define NVS_KEY_DEV_SEED     "device_seed"
#define DIAG_MODULE_OPENCLAW 0
#define DIAG_LOG_INFO 0
#define DIAG_LOG_WARN 1
#define DIAG_LOG_ERROR 2
#define diag_log(...) ((void)0)
#define diag_set_module_status(...) ((void)0)

#define EVT_ANSWER_READY     BIT0
#define EVT_PAIR_CODE        BIT1
#define EVT_PAIR_DONE        BIT2
#define EVT_CONNECTED        BIT3
#define EVT_DISCONNECTED     BIT4
#define EVT_CHAT_READY       BIT5
#define EVT_TERMINAL_READY   BIT6
#define EVT_TERMINAL_ERROR   BIT7

// ── Internal state ────────────────────────────────────────────────────────────

static esp_websocket_client_handle_t s_ws         = NULL;
static volatile openclaw_state_t     s_state      = OPENCLAW_STATE_DISCONNECTED;
static EventGroupHandle_t            s_evg        = NULL;

static char s_token[OPENCLAW_TOKEN_MAXLEN]        = {0};
static char s_gateway_token[OPENCLAW_TOKEN_MAXLEN] = {0};
static char s_host[64]                            = OPENCLAW_HOST;
static uint16_t s_port                            = OPENCLAW_PORT;
static char s_device_id[65]                       = {0};
static char s_device_pub_b64url[64]               = {0};
static uint8_t s_device_seed[32]                  = {0};
static uint8_t s_device_pub_raw[32]               = {0};
static char s_answer_buf[1024]                    = {0};
static char s_pair_code[64]                       = {0};
static char s_connect_req_id[32]                  = {0};
static char s_chat_req_id[32]                     = {0};
static char s_chat_run_id[64]                     = {0};
static uint32_t s_req_seq                          = 1;
#define OPENCLAW_WS_MSG_BUF_SIZE 16384
#define OPENCLAW_TERMINAL_AUDIO_B64_MAX 8192
static EXT_RAM_BSS_ATTR char s_ws_msg_buf[OPENCLAW_WS_MSG_BUF_SIZE] = {0};
static EXT_RAM_BSS_ATTR char s_terminal_audio_b64[OPENCLAW_TERMINAL_AUDIO_B64_MAX] = {0};
static int s_ws_msg_len                            = 0;
static bool s_ws_drop_payload                      = false;
static uint32_t s_ws_data_events                   = 0;
static uint32_t s_ws_challenge_events              = 0;
static uint32_t s_ws_connect_attempts              = 0;
static int s_last_ws_close_code                    = 0;
static int64_t s_last_ws_connected_ms              = 0;
static int64_t s_last_challenge_rx_ms              = 0;
static int64_t s_last_connect_req_ms               = 0;
static const char *s_auth_stage                    = "idle";
static int64_t s_chat_block_until_ms               = 0;
static char s_chat_block_reason[48]                = {0};
static openclaw_terminal_event_cb_t s_terminal_cb  = NULL;
static void *s_terminal_user_ctx                   = NULL;
static openclaw_terminal_event_cb_t s_terminal_listener_cb = NULL;
static void *s_terminal_listener_ctx               = NULL;
static bool s_terminal_active                      = false;
static char s_terminal_session_id[64]              = {0};
static char s_terminal_open_req_id[32]             = {0};
static char s_terminal_error[160]                  = {0};

static const char *OPENCLAW_CLIENT_ID = "gateway-client";
static const char *OPENCLAW_CLIENT_MODE = "cli";
static const char *OPENCLAW_ROLE = "operator";
static const char *OPENCLAW_SCOPES_CSV = "operator.read,operator.write,operator.admin";
static const int OPENCLAW_PROTOCOL_VERSION = 3;
static const int64_t OPENCLAW_MIN_EPOCH_SEC = 1700000000LL; // 2023-11-14 UTC

static openclaw_pair_code_cb_t s_on_pair_code     = NULL;
static openclaw_pair_done_cb_t s_on_pair_done     = NULL;

static bool openclaw_send_device_method(const char *method, const char *body_fields_json);

static int64_t oc_now_ms(void)
{
    return (int64_t)(esp_timer_get_time() / 1000ULL);
}

static uint64_t oc_epoch_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static bool oc_epoch_is_synced(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec >= OPENCLAW_MIN_EPOCH_SEC;
}

static void openclaw_register_results_listener(void)
{
    char params[160];

    if (!s_ws || s_device_id[0] == '\0') {
        return;
    }

    snprintf(params, sizeof(params), "\"deviceId\":\"%s\"", s_device_id);
    if (!openclaw_send_device_method("deskbot.results.register", params)) {
        ESP_LOGW(TAG, "deskbot.results.register send failed for device=%s", s_device_id);
    } else {
        ESP_LOGI(TAG, "deskbot.results.register sent for device=%s", s_device_id);
    }
}

static void openclaw_ws_teardown(void)
{
    if (s_ws) {
        esp_websocket_client_stop(s_ws);
        esp_websocket_client_destroy(s_ws);
        s_ws = NULL;
    }
}

static void openclaw_block_chat(const char *reason, int64_t duration_ms)
{
    if (duration_ms < 0) duration_ms = 0;
    s_chat_block_until_ms = oc_now_ms() + duration_ms;
    strncpy(s_chat_block_reason, reason ? reason : "blocked", sizeof(s_chat_block_reason) - 1);
    s_chat_block_reason[sizeof(s_chat_block_reason) - 1] = '\0';
}

static const char *oc_state_name(openclaw_state_t state) __attribute__((unused));
static const char *oc_state_name(openclaw_state_t state)
{
    switch (state) {
    case OPENCLAW_STATE_DISCONNECTED: return "DISCONNECTED";
    case OPENCLAW_STATE_CONNECTING: return "CONNECTING";
    case OPENCLAW_STATE_PAIRING: return "PAIRING";
    case OPENCLAW_STATE_READY: return "READY";
    case OPENCLAW_STATE_BUSY: return "BUSY";
    default: return "UNKNOWN";
    }
}

static void oc_diag_status(const char *detail)
{
    const char *d = detail;
    if (!d || d[0] == '\0') {
        d = s_auth_stage;
    }
    diag_set_module_status(DIAG_MODULE_OPENCLAW, oc_state_name(s_state), d);
}

// ── JSON helpers ──────────────────────────────────────────────────────────────

void openclaw_json_escape(const char *src, char *dst, size_t dst_sz)
{
    if (!src || !dst || dst_sz < 2) { if (dst && dst_sz) dst[0] = '\0'; return; }
    size_t wi = 0;
    for (size_t i = 0; src[i] && wi + 2 < dst_sz; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            if (wi + 3 >= dst_sz) break;
            dst[wi++] = '\\'; dst[wi++] = (char)c;
        } else if (c < 0x20) {
            dst[wi++] = ' ';
        } else {
            dst[wi++] = (char)c;
        }
    }
    dst[wi] = '\0';
}

// Extract a JSON string field value into out[out_sz].
static bool json_get_str(const char *json, const char *key, char *out, size_t out_sz)
{
    if (!json || !key || !out || out_sz < 2) return false;
    const char *k = strstr(json, key);
    if (!k) return false;
    const char *c = strchr(k, ':');
    if (!c) return false;
    c++;
    while (*c == ' ' || *c == '\t') c++;
    if (*c != '"') return false;
    c++;
    size_t wi = 0;
    while (*c && wi + 1 < out_sz) {
        if (*c == '\\' && *(c+1)) { c++; out[wi++] = *c; }
        else if (*c == '"') break;
        else out[wi++] = *c;
        c++;
    }
    out[wi] = '\0';
    return wi > 0;
}

static bool json_get_bool(const char *json, const char *key, bool *out)
{
    if (!json || !key || !out) return false;
    const char *k = strstr(json, key);
    if (!k) return false;
    const char *c = strchr(k, ':');
    if (!c) return false;
    c++;
    while (*c == ' ' || *c == '\t') c++;
    if (strncmp(c, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(c, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

static const char *json_type_name(const char *json, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return "";
    out[0] = '\0';
    if (json_get_str(json, "\"type\"", out, out_sz)) {
        return out;
    }
    return "";
}

static const char *json_event_name(const char *json, char *out, size_t out_sz)
{
    if (!out || out_sz == 0) return "";
    out[0] = '\0';
    if (json_get_str(json, "\"event\"", out, out_sz)) {
        return out;
    }
    return "";
}

static void json_skip_ws(const char **p)
{
    while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n') (*p)++;
}

static bool json_parse_string(const char **p, char *out, size_t out_sz)
{
    if (!p || !*p || **p != '"' || !out || out_sz < 2) return false;
    (*p)++;
    size_t wi = 0;
    while (**p) {
        char c = **p;
        if (c == '\\') {
            (*p)++;
            if (!**p) break;
            c = **p;
            if (wi + 1 < out_sz) out[wi++] = c;
            (*p)++;
            continue;
        }
        if (c == '"') {
            (*p)++;
            out[wi] = '\0';
            return true;
        }
        if (wi + 1 < out_sz) out[wi++] = c;
        (*p)++;
    }
    out[0] = '\0';
    return false;
}

static bool json_skip_value(const char **p)
{
    if (!p || !*p || !**p) return false;
    json_skip_ws(p);
    if (**p == '"') {
        char tmp[2];
        return json_parse_string(p, tmp, sizeof(tmp));
    }
    if (**p == '{' || **p == '[') {
        char open = **p;
        char close = (open == '{') ? '}' : ']';
        int depth = 0;
        bool in_str = false;
        bool esc = false;
        while (**p) {
            char c = **p;
            (*p)++;
            if (in_str) {
                if (esc) esc = false;
                else if (c == '\\') esc = true;
                else if (c == '"') in_str = false;
                continue;
            }
            if (c == '"') { in_str = true; continue; }
            if (c == open) depth++;
            else if (c == close) {
                depth--;
                if (depth == 0) return true;
            }
        }
        return false;
    }
    while (**p && **p != ',' && **p != '}') (*p)++;
    return true;
}

static bool json_top_level_get_str(const char *json, const char *key, char *out, size_t out_sz)
{
    if (!json || !key || !out || out_sz < 2) return false;
    const char *p = json;
    json_skip_ws(&p);
    if (*p != '{') return false;
    p++;
    while (*p) {
        json_skip_ws(&p);
        if (*p == '}') break;
        if (*p == ',') { p++; continue; }
        char k[64] = {0};
        if (!json_parse_string(&p, k, sizeof(k))) return false;
        json_skip_ws(&p);
        if (*p != ':') return false;
        p++;
        json_skip_ws(&p);
        if (strcmp(k, key) == 0) {
            return json_parse_string(&p, out, out_sz);
        }
        if (!json_skip_value(&p)) return false;
        json_skip_ws(&p);
        if (*p == ',') p++;
    }
    return false;
}

static bool __attribute__((unused)) json_top_level_get_bool(const char *json, const char *key, bool *out)
{
    if (!json || !key || !out) return false;
    const char *p = json;
    json_skip_ws(&p);
    if (*p != '{') return false;
    p++;
    while (*p) {
        json_skip_ws(&p);
        if (*p == '}') break;
        if (*p == ',') { p++; continue; }
        char k[64] = {0};
        if (!json_parse_string(&p, k, sizeof(k))) return false;
        json_skip_ws(&p);
        if (*p != ':') return false;
        p++;
        json_skip_ws(&p);
        if (strcmp(k, key) == 0) {
            if (strncmp(p, "true", 4) == 0) { *out = true; return true; }
            if (strncmp(p, "false", 5) == 0) { *out = false; return true; }
            return false;
        }
        if (!json_skip_value(&p)) return false;
        json_skip_ws(&p);
        if (*p == ',') p++;
    }
    return false;
}

static bool json_top_level_has_key(const char *json, const char *key)
{
    if (!json || !key) return false;
    const char *p = json;
    json_skip_ws(&p);
    if (*p != '{') return false;
    p++;
    while (*p) {
        json_skip_ws(&p);
        if (*p == '}') break;
        if (*p == ',') { p++; continue; }
        char k[64] = {0};
        if (!json_parse_string(&p, k, sizeof(k))) return false;
        json_skip_ws(&p);
        if (*p != ':') return false;
        p++;
        if (strcmp(k, key) == 0) return true;
        if (!json_skip_value(&p)) return false;
        json_skip_ws(&p);
        if (*p == ',') p++;
    }
    return false;
}

static bool json_type_is(const char *json, const char *expected)
{
    char t[24] = {0};
    if (!json || !expected) return false;
    if (!json_top_level_get_str(json, "type", t, sizeof(t))) return false;
    return strcmp(t, expected) == 0;
}

static bool json_event_is(const char *json, const char *expected)
{
    char e[64] = {0};
    if (!json || !expected) return false;
    if (!json_top_level_get_str(json, "event", e, sizeof(e))) return false;
    return strcmp(e, expected) == 0;
}

static bool json_id_matches(const char *json, const char *expected_id)
{
    if (!json || !expected_id || expected_id[0] == '\0') return false;
    char id[64] = {0};
    if (!json_top_level_get_str(json, "id", id, sizeof(id))) return false;
    return strcmp(id, expected_id) == 0;
}

static bool terminal_event_matches_active_session(const char *json)
{
    char terminal_session_id[64] = {0};

    if (!json || !s_terminal_active || s_terminal_session_id[0] == '\0') return false;
    if (!json_get_str(json, "\"terminalSessionId\"", terminal_session_id, sizeof(terminal_session_id))) return false;
    if (terminal_session_id[0] == '\0') return false;
    return strcmp(terminal_session_id, s_terminal_session_id) == 0;
}

static bool openclaw_should_log_rx_frame(const char *type_name, const char *event_name)
{
    if (!type_name || type_name[0] == '\0') {
        return false;
    }
    if (strcmp(type_name, "error") == 0) {
        return true;
    }
    if (strcmp(type_name, "event") != 0 || !event_name || event_name[0] == '\0') {
        return false;
    }
    return strcmp(event_name, "connect.challenge") == 0 ||
           strcmp(event_name, "chat") == 0 ||
           strcmp(event_name, "deskbot.results.pending") == 0 ||
           strcmp(event_name, "deskbot.results.ready") == 0 ||
           strcmp(event_name, "deskbot.results.clear") == 0 ||
           strcmp(event_name, "deskbot.results.replay") == 0 ||
           strcmp(event_name, "deskbot.session.error") == 0;
}

// ── NVS token storage ─────────────────────────────────────────────────────────

static void token_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;
    size_t len = sizeof(s_token);
    nvs_get_str(h, NVS_KEY_TOKEN, s_token, &len);
    len = sizeof(s_gateway_token);
    nvs_get_str(h, NVS_KEY_GW_TOKEN, s_gateway_token, &len);
    nvs_close(h);
    if (strlen(s_token) > 0) {
        ESP_LOGI(TAG, "OpenClaw token loaded from NVS");
    }
    if (strlen(s_gateway_token) > 0) {
        ESP_LOGI(TAG, "OpenClaw gateway token loaded from NVS");
    }
}

static void token_save(const char *token)
{
    strncpy(s_token, token, sizeof(s_token) - 1);
    s_token[sizeof(s_token) - 1] = '\0';
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_TOKEN, s_token);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "OpenClaw token saved to NVS");
}

void openclaw_forget_token(void)
{
    memset(s_token, 0, sizeof(s_token));
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, NVS_KEY_TOKEN);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "OpenClaw token erased");
}

void openclaw_set_gateway_token(const char *token)
{
    if (!token) token = "";
    strncpy(s_gateway_token, token, sizeof(s_gateway_token) - 1);
    s_gateway_token[sizeof(s_gateway_token) - 1] = '\0';

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_GW_TOKEN, s_gateway_token);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "OpenClaw gateway token %s", s_gateway_token[0] ? "saved" : "cleared");
}

bool openclaw_has_gateway_token(void)
{
    return s_gateway_token[0] != '\0';
}

bool openclaw_set_endpoint(const char *host, uint16_t port)
{
    if (!host || host[0] == '\0') {
        s_host[0] = '\0';
        s_port = 0;
        return true;
    }
    strncpy(s_host, host, sizeof(s_host) - 1);
    s_host[sizeof(s_host) - 1] = '\0';
    s_port = port;
    return true;
}

const char *openclaw_get_endpoint_host(void)
{
    return s_host;
}

uint16_t openclaw_get_endpoint_port(void)
{
    return s_port;
}

bool openclaw_has_token(void)
{
    return s_token[0] != '\0';
}

static bool base64_to_base64url(const unsigned char *in, size_t in_len, char *out, size_t out_sz);

static bool sha256_bytes(const uint8_t *input, size_t input_len, uint8_t output[32])
{
    if (!input || !output) {
        return false;
    }

    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!md_info) {
        return false;
    }

    return mbedtls_md(md_info, input, input_len, output) == 0;
}

static void init_device_id_once(void)
{
    if (s_device_id[0] != '\0') {
        return;
    }
    uint8_t seed[32] = {0};
    bool loaded = false;
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(seed);
        if (nvs_get_blob(h, NVS_KEY_DEV_SEED, seed, &len) == ESP_OK && len == sizeof(seed)) {
            loaded = true;
        }
        nvs_close(h);
    }
    if (!loaded) {
        esp_fill_random(seed, sizeof(seed));
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_blob(h, NVS_KEY_DEV_SEED, seed, sizeof(seed));
            nvs_commit(h);
            nvs_close(h);
        }
    }

    uint8_t seed_copy[32] = {0};
    uint8_t private_key[ED25519_PRIVATE_KEY_SIZE] = {0};
    memcpy(seed_copy, seed, sizeof(seed_copy));
    compact_ed25519_keygen(private_key, s_device_pub_raw, seed_copy);
    memcpy(s_device_seed, seed, sizeof(s_device_seed));
    memset(seed_copy, 0, sizeof(seed_copy));
    memset(seed, 0, sizeof(seed));

    uint8_t hash[32] = {0};
    if (!sha256_bytes(s_device_pub_raw, sizeof(s_device_pub_raw), hash)) {
        ESP_LOGE(TAG, "OpenClaw device identity hash failed");
        memset(private_key, 0, sizeof(private_key));
        memset(hash, 0, sizeof(hash));
        return;
    }
    for (size_t i = 0; i < sizeof(hash); i++) {
        snprintf(s_device_id + i * 2, sizeof(s_device_id) - i * 2, "%02x", hash[i]);
    }
    base64_to_base64url(s_device_pub_raw, sizeof(s_device_pub_raw),
                        s_device_pub_b64url, sizeof(s_device_pub_b64url));
    memset(private_key, 0, sizeof(private_key));
    memset(hash, 0, sizeof(hash));

    ESP_LOGI(TAG, "OpenClaw device identity ready id=%s", s_device_id);
}

static bool base64_to_base64url(const unsigned char *in, size_t in_len, char *out, size_t out_sz)
{
    if (!in || !out || out_sz < 4U) {
        return false;
    }
    size_t olen = 0;
    if (mbedtls_base64_encode((unsigned char *)out, out_sz - 1U, &olen, in, in_len) != 0) {
        return false;
    }
    out[olen] = '\0';
    for (size_t i = 0; i < olen; i++) {
        if (out[i] == '+') out[i] = '-';
        else if (out[i] == '/') out[i] = '_';
    }
    while (olen > 0 && out[olen - 1] == '=') {
        out[--olen] = '\0';
    }
    return true;
}

static void regenerate_device_identity(void)
{
    uint8_t seed[32] = {0};
    esp_fill_random(seed, sizeof(seed));

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_KEY_DEV_SEED, seed, sizeof(seed));
        nvs_commit(h);
        nvs_close(h);
    }

    memset(s_device_id, 0, sizeof(s_device_id));
    memset(s_device_pub_b64url, 0, sizeof(s_device_pub_b64url));
    memset(s_device_seed, 0, sizeof(s_device_seed));
    memset(s_device_pub_raw, 0, sizeof(s_device_pub_raw));
    init_device_id_once();
    memset(seed, 0, sizeof(seed));
}

static bool ed25519_sign_payload_b64url(const char *payload, char *sig_out, size_t sig_out_sz)
{
    if (!payload || !sig_out || sig_out_sz < 16U) {
        return false;
    }
    uint8_t private_key[ED25519_PRIVATE_KEY_SIZE] = {0};
    memcpy(private_key, s_device_seed, 32);
    memcpy(private_key + 32, s_device_pub_raw, 32);

    uint8_t sig_bin[96] = {0};
    compact_ed25519_sign(sig_bin, private_key, payload, strlen(payload));
    memset(private_key, 0, sizeof(private_key));
    return base64_to_base64url(sig_bin, ED25519_SIGNATURE_SIZE, sig_out, sig_out_sz);
}

static bool openclaw_send_req_frame(const char *id, const char *method, const char *params_json)
{
    if (!s_ws || !id || !method) {
        return false;
    }

    const size_t params_len = params_json ? strlen(params_json) : 0;
    const size_t frame_cap = strlen(id) + strlen(method) + params_len + 64;
    char *frame = malloc(frame_cap);
    if (!frame) {
        ESP_LOGE(TAG, "openclaw_send_req_frame OOM for method=%s", method);
        return false;
    }

    int n = snprintf(frame,
                     frame_cap,
                     "{\"type\":\"req\",\"id\":\"%s\",\"method\":\"%s\"%s%s}",
                     id,
                     method,
                     params_json ? ",\"params\":" : "",
                     params_json ? params_json : "");
    if (n <= 0 || n >= (int)frame_cap) {
        free(frame);
        return false;
    }

    int rc = esp_websocket_client_send_text(s_ws, frame, n, pdMS_TO_TICKS(2000));
    free(frame);
    if (method && strcmp(method, "connect") == 0) {
        ESP_LOGW(TAG, "OpenClaw: send connect frame rc=%d bytes=%d id=%s", rc, n, id ? id : "-");
    }
    return rc >= 0;
}

static bool openclaw_base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_sz, size_t *out_len)
{
    if (!in || in_len == 0 || !out || out_sz < 8) return false;
    size_t olen = 0;
    if (mbedtls_base64_encode((unsigned char *)out, out_sz - 1U, &olen, in, in_len) != 0) {
        return false;
    }
    out[olen] = '\0';
    if (out_len) *out_len = olen;
    return true;
}

static void openclaw_terminal_emit(openclaw_terminal_event_type_t type,
                                   const uint8_t *data,
                                   size_t data_len)
{
    if (s_terminal_listener_cb) {
        s_terminal_listener_cb(type, data, data_len, s_terminal_listener_ctx);
    }
    if (s_terminal_cb && s_terminal_cb != s_terminal_listener_cb) {
        s_terminal_cb(type, data, data_len, s_terminal_user_ctx);
    }
}

bool openclaw_terminal_register_listener(openclaw_terminal_event_cb_t cb, void *user_ctx)
{
    s_terminal_listener_cb = cb;
    s_terminal_listener_ctx = user_ctx;
    return true;
}

static bool openclaw_terminal_send_method(const char *method, const char *body_fields_json)
{
    if (!s_ws || !method || !s_terminal_active || s_terminal_session_id[0] == '\0') {
        return false;
    }

    char req_id[32];
    snprintf(req_id, sizeof(req_id), "oc-term-%lu", (unsigned long)s_req_seq++);

    const size_t body_len = body_fields_json ? strlen(body_fields_json) : 0;
    const size_t params_cap = strlen(s_device_id) + strlen(s_terminal_session_id) + body_len + 128;
    char *params = malloc(params_cap);
    if (!params) {
        ESP_LOGE(TAG, "terminal params OOM for %s", method);
        return false;
    }

    int n = snprintf(params, params_cap,
                     "{"
                     "\"sessionKey\":\"agent:main:deskbot\","
                     "\"deviceId\":\"%s\","
                     "\"terminalSessionId\":\"%s\"%s%s"
                     "}",
                     s_device_id,
                     s_terminal_session_id,
                     body_fields_json ? "," : "",
                     body_fields_json ? body_fields_json : "");
    if (n <= 0 || n >= (int)params_cap) {
        ESP_LOGE(TAG, "terminal params overflow for %s", method);
        free(params);
        return false;
    }

    bool ok = openclaw_send_req_frame(req_id, method, params);
    free(params);
    return ok;
}

static bool openclaw_send_device_method(const char *method, const char *body_fields_json)
{
    if (!s_ws || !method || s_device_id[0] == '\0') {
        return false;
    }

    char req_id[32];
    snprintf(req_id, sizeof(req_id), "oc-dev-%lu", (unsigned long)s_req_seq++);

    const size_t body_len = body_fields_json ? strlen(body_fields_json) : 0;
    const size_t params_cap = strlen(s_device_id) + body_len + 96;
    char *params = malloc(params_cap);
    if (!params) {
        ESP_LOGE(TAG, "device params OOM for %s", method);
        return false;
    }

    int n = snprintf(params, params_cap,
                     "{"
                     "\"deviceId\":\"%s\"%s%s"
                     "}",
                     s_device_id,
                     body_fields_json ? "," : "",
                     body_fields_json ? body_fields_json : "");
    if (n <= 0 || n >= (int)params_cap) {
        free(params);
        return false;
    }

    bool ok = openclaw_send_req_frame(req_id, method, params);
    free(params);
    return ok;
}

bool openclaw_results_replay(const char *task_id)
{
    char fields[128];

    if (!task_id || task_id[0] == '\0') {
        return false;
    }

    snprintf(fields, sizeof(fields), "\"taskId\":\"%s\"", task_id);
    return openclaw_send_device_method("deskbot.results.replay", fields);
}

bool openclaw_results_consume(const char *task_id)
{
    char fields[192];

    if (!task_id || task_id[0] == '\0') {
        return false;
    }

    snprintf(fields,
             sizeof(fields),
             "\"taskId\":\"%s\",\"consumedBy\":\"%s\"",
             task_id,
             s_device_id[0] != '\0' ? s_device_id : "device");
    return openclaw_send_device_method("deskbot.results.consume", fields);
}

static void openclaw_send_connect_req(const char *nonce)
{
    s_auth_stage = "sending_connect";
    oc_diag_status(NULL);
    s_ws_connect_attempts++;
    // Prefer device-issued token after pairing; gateway token is bootstrap fallback.
    const char *auth_token = s_token[0] ? s_token : s_gateway_token;
    uint64_t signed_at_ms = oc_epoch_ms();
    if (!nonce || nonce[0] == '\0') {
        ESP_LOGW(TAG, "OpenClaw: connect requires challenge nonce");
        return;
    }
    if (!oc_epoch_is_synced()) {
        s_auth_stage = "waiting_time_sync";
        s_state = OPENCLAW_STATE_DISCONNECTED;
        openclaw_block_chat("time_sync_pending", 5000);
        diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_WARN,
                 "connect blocked: epoch unsynced signedAt=%llu",
                 (unsigned long long)signed_at_ms);
        oc_diag_status("time_sync_pending");
        ESP_LOGW(TAG, "OpenClaw: connect blocked until SNTP sync (signedAt=%llu)",
                 (unsigned long long)signed_at_ms);
        openclaw_ws_teardown();
        return;
    }
    // ESP_LOGW(TAG, "OpenClaw: connect attempt #%lu nonce_len=%u signedAt=%llu token=%s",
    //          (unsigned long)s_ws_connect_attempts,
    //          (unsigned)strlen(nonce),
    //          (unsigned long long)signed_at_ms,
    //          auth_token[0] ? "set" : "EMPTY");

    // Must match gateway buildDeviceAuthPayload():
    // v2|deviceId|clientId|clientMode|role|scopesCsv|signedAtMs|token|nonce
    char auth_payload[640];
    int apn = snprintf(auth_payload,
                       sizeof(auth_payload),
                       "v2|%s|%s|%s|%s|%s|%llu|%s|%s",
                       s_device_id,
                       OPENCLAW_CLIENT_ID,
                       OPENCLAW_CLIENT_MODE,
                       OPENCLAW_ROLE,
                       OPENCLAW_SCOPES_CSV,
                       (unsigned long long)signed_at_ms,
                       auth_token[0] ? auth_token : "",
                       nonce);
    if (apn <= 0 || apn >= (int)sizeof(auth_payload)) {
        ESP_LOGW(TAG, "OpenClaw: auth payload overflow");
        return;
    }

    char sig_b64url[192] = {0};
    if (!ed25519_sign_payload_b64url(auth_payload, sig_b64url, sizeof(sig_b64url))) {
        ESP_LOGW(TAG, "OpenClaw: connect signature failed");
        return;
    }

    char nonce_field[192] = {0};
    int nn = snprintf(nonce_field, sizeof(nonce_field), ",\"nonce\":\"%s\"", nonce);
    if (nn <= 0 || nn >= (int)sizeof(nonce_field)) {
        ESP_LOGW(TAG, "OpenClaw: nonce field overflow");
        return;
    }

    char params[1024];
    int n;
    if (auth_token[0] != '\0') {
        n = snprintf(params,
                     sizeof(params),
                     "{"
                     "\"minProtocol\":%d,"
                     "\"maxProtocol\":%d,"
                     "\"client\":{\"id\":\"%s\",\"displayName\":\"ReSono Labs Syntax\",\"version\":\"esp-idf\",\"platform\":\"esp32s3\",\"mode\":\"%s\"},"
                     "\"role\":\"%s\","
                     "\"scopes\":[\"operator.read\",\"operator.write\",\"operator.admin\"],"
                     "\"caps\":[\"tool-events\"],"
                     "\"auth\":{\"token\":\"%s\"},"
                     "\"device\":{\"id\":\"%s\",\"publicKey\":\"%s\",\"signature\":\"%s\",\"signedAt\":%llu%s}"
                     "}",
                     OPENCLAW_PROTOCOL_VERSION,
                     OPENCLAW_PROTOCOL_VERSION,
                     OPENCLAW_CLIENT_ID,
                     OPENCLAW_CLIENT_MODE,
                     OPENCLAW_ROLE,
                     auth_token,
                     s_device_id,
                     s_device_pub_b64url,
                     sig_b64url,
                     (unsigned long long)signed_at_ms,
                     nonce_field);
    } else {
        n = snprintf(params,
                     sizeof(params),
                     "{"
                     "\"minProtocol\":%d,"
                     "\"maxProtocol\":%d,"
                     "\"client\":{\"id\":\"%s\",\"displayName\":\"ReSono Labs Syntax\",\"version\":\"esp-idf\",\"platform\":\"esp32s3\",\"mode\":\"%s\"},"
                     "\"role\":\"%s\","
                     "\"scopes\":[\"operator.read\",\"operator.write\",\"operator.admin\"],"
                     "\"caps\":[\"tool-events\"],"
                     "\"device\":{\"id\":\"%s\",\"publicKey\":\"%s\",\"signature\":\"%s\",\"signedAt\":%llu%s}"
                     "}",
                     OPENCLAW_PROTOCOL_VERSION,
                     OPENCLAW_PROTOCOL_VERSION,
                     OPENCLAW_CLIENT_ID,
                     OPENCLAW_CLIENT_MODE,
                     OPENCLAW_ROLE,
                     s_device_id,
                     s_device_pub_b64url,
                     sig_b64url,
                     (unsigned long long)signed_at_ms,
                     nonce_field);
    }
    if (n <= 0 || n >= (int)sizeof(params)) {
        ESP_LOGW(TAG, "OpenClaw: connect params overflow");
        return;
    }

    snprintf(s_connect_req_id, sizeof(s_connect_req_id), "oc-connect-%lu", (unsigned long)s_req_seq++);
    int64_t send_ts_ms = oc_now_ms();
    if (!openclaw_send_req_frame(s_connect_req_id, "connect", params)) {
        s_auth_stage = "connect_send_failed";
        diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_WARN, "connect request send failed");
        oc_diag_status(NULL);
        ESP_LOGW(TAG, "OpenClaw: connect req send failed");
    } else {
        s_auth_stage = "connect_sent";
        s_last_connect_req_ms = send_ts_ms;
        diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_INFO, "connect request sent");
        oc_diag_status(NULL);
        // ESP_LOGI(TAG, "OpenClaw: connect req sent id=%s", s_connect_req_id);
    }
}

// ── WebSocket event handler ───────────────────────────────────────────────────

static void ws_event_handler(void *arg,
                             esp_event_base_t base,
                             int32_t event_id,
                             void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "OpenClaw WS connected");
        s_auth_stage = "ws_connected_waiting_challenge";
        s_last_ws_connected_ms = oc_now_ms();
        s_ws_data_events = 0;
        s_ws_challenge_events = 0;
        s_ws_connect_attempts = 0;
        s_state = OPENCLAW_STATE_CONNECTING;
        diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_INFO, "ws connected");
        oc_diag_status(NULL);
        // Keep ping at 1s during auth to flush connect.challenge promptly.
        esp_websocket_client_set_ping_interval_sec(s_ws, 1);
        xEventGroupSetBits(s_evg, EVT_CONNECTED);
        // ESP_LOGI(TAG, "OpenClaw: waiting for connect.challenge");
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "OpenClaw WS disconnected");
        s_auth_stage = "ws_disconnected";
        s_state = OPENCLAW_STATE_DISCONNECTED;
        s_terminal_active = false;
        s_terminal_session_id[0] = '\0';
        diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_WARN, "ws disconnected");
        oc_diag_status(NULL);
        xEventGroupSetBits(s_evg, EVT_DISCONNECTED);
        openclaw_terminal_emit(OPENCLAW_TERMINAL_EVENT_ERROR,
                               (const uint8_t *)"openclaw_ws_disconnected",
                               strlen("openclaw_ws_disconnected"));
        break;

    case WEBSOCKET_EVENT_DATA:
        if (!data || !data->data_ptr || data->data_len <= 0) break;
        {
            if (data->op_code == 0x08) {
                if (data->data_len >= 2) {
                    s_last_ws_close_code = ((uint8_t)data->data_ptr[0] << 8) |
                                           (uint8_t)data->data_ptr[1];
                }
                ESP_LOGW(TAG, "OpenClaw: WS close frame code=%d", s_last_ws_close_code);
                diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_WARN, "ws close code=%d", s_last_ws_close_code);
                break;
            }
            s_ws_data_events++;
            if (s_state == OPENCLAW_STATE_CONNECTING) {
                // ESP_LOGW(TAG, "OpenClaw: ws data#%lu len=%d payload_len=%d off=%d fin=%d op=%u",
                //          (unsigned long)s_ws_data_events,
                //          data->data_len,
                //          data->payload_len,
                //          data->payload_offset,
                //          data->fin ? 1 : 0,
                //          (unsigned)data->op_code);
            }
            // Reassemble fragmented websocket payloads (challenge/events may span chunks).
            if (data->payload_offset == 0) {
                s_ws_msg_len = 0;
                s_ws_msg_buf[0] = '\0';
                s_ws_drop_payload = false;
                if (data->payload_len >= (int)sizeof(s_ws_msg_buf)) {
                    s_ws_drop_payload = true;
                    ESP_LOGW(TAG, "OpenClaw: ws payload too large (%d >= %u), dropping frame",
                             data->payload_len, (unsigned)sizeof(s_ws_msg_buf));
                }
            }
            if (data->payload_offset < 0 || data->payload_len < 0) {
                break;
            }
            if (s_ws_drop_payload) {
                break;
            }
            if (data->payload_offset >= (int)sizeof(s_ws_msg_buf) - 1) {
                ESP_LOGW(TAG, "OpenClaw: ws payload offset out of range");
                break;
            }
            int copy_len = data->data_len;
            int room = (int)sizeof(s_ws_msg_buf) - 1 - data->payload_offset;
            if (copy_len > room) {
                copy_len = room;
            }
            if (copy_len <= 0) {
                ESP_LOGW(TAG, "OpenClaw: ws payload too large");
                break;
            }
            memcpy(s_ws_msg_buf + data->payload_offset, data->data_ptr, copy_len);
            int write_end = data->payload_offset + copy_len;
            if (write_end > s_ws_msg_len) {
                s_ws_msg_len = write_end;
            }
            s_ws_msg_buf[s_ws_msg_len] = '\0';

            bool frame_complete = true;
            if (data->payload_len > 0) {
                frame_complete = ((data->payload_offset + data->data_len) >= data->payload_len) && data->fin;
            } else if (!data->fin) {
                frame_complete = false;
            }
            if (!frame_complete) {
                break;
            }
            const char *msg = s_ws_msg_buf;
            char type_name[32] = {0};
            char event_name[96] = {0};
            json_type_name(msg, type_name, sizeof(type_name));
            json_event_name(msg, event_name, sizeof(event_name));
            if (s_state == OPENCLAW_STATE_CONNECTING) {
                ESP_LOGI(TAG, "OpenClaw RX frame type=%s event=%s len=%d",
                         type_name[0] ? type_name : "-",
                         event_name[0] ? event_name : "-",
                         s_ws_msg_len);
            } else if (openclaw_should_log_rx_frame(type_name, event_name)) {
                ESP_LOGI(TAG, "OpenClaw RX frame type=%s event=%s len=%d",
                         type_name,
                         event_name[0] ? event_name : "-",
                         s_ws_msg_len);
            }
            if (json_type_is(msg, "event") && json_event_is(msg, "connect.challenge")) {
                char nonce[128] = {0};
                if (!json_get_str(msg, "\"nonce\"", nonce, sizeof(nonce))) {
                    ESP_LOGW(TAG, "OpenClaw: malformed connect.challenge");
                    break;
                }
                s_ws_challenge_events++;
                s_auth_stage = "challenge_received";
                s_last_challenge_rx_ms = oc_now_ms();
                diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_INFO, "challenge received");
                oc_diag_status(NULL);
                // ESP_LOGW(TAG, "OpenClaw: connect challenge #%lu received", (unsigned long)s_ws_challenge_events);
                openclaw_send_connect_req(nonce);
                break;
            }

            // Handle connect response — reference logic:
            // res with NO "error" key = success (hello-ok).
            // res WITH "error" key = rejected; check details for NOT_PAIRED/requestId.
            // Only process when id matches our connect request.
            if (json_type_is(msg, "res") && json_id_matches(msg, s_connect_req_id)) {
                bool has_error = json_top_level_has_key(msg, "error");
                if (!has_error) {
                    // Success — hello-ok
                    s_chat_block_until_ms = 0;
                    s_chat_block_reason[0] = '\0';
                    s_auth_stage = "connected_ready";
                    char new_token[OPENCLAW_TOKEN_MAXLEN] = {0};
                    if (json_get_str(msg, "\"deviceToken\"", new_token, sizeof(new_token)) &&
                        strlen(new_token) > 0U) {
                        token_save(new_token);
                    }
                    s_state = OPENCLAW_STATE_READY;
                    diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_INFO, "connected ready");
                    oc_diag_status(NULL);
                    ESP_LOGI(TAG, "OpenClaw: native connect ok, state=READY");
                    esp_websocket_client_set_ping_interval_sec(s_ws, 30);
                    openclaw_register_results_listener();
                    if (s_on_pair_done) s_on_pair_done();
                    xEventGroupSetBits(s_evg, EVT_PAIR_DONE);
                } else {
                    // Error — extract code/message/requestId from error object or top level
                    char req_id[64] = {0};
                    char err_code[40] = {0};
                    char err_msg[120] = {0};
                    // Try nested: {"error":{"code":...,"message":...,"details":{"requestId":...}}}
                    const char *err_obj = strstr(msg, "\"error\"");
                    if (err_obj) {
                        json_get_str(err_obj, "\"code\"", err_code, sizeof(err_code));
                        json_get_str(err_obj, "\"message\"", err_msg, sizeof(err_msg));
                        json_get_str(err_obj, "\"requestId\"", req_id, sizeof(req_id));
                    }
                    // Fallback: top-level fields
                    if (err_code[0] == '\0') json_get_str(msg, "\"code\"", err_code, sizeof(err_code));
                    if (err_msg[0] == '\0')  json_get_str(msg, "\"message\"", err_msg, sizeof(err_msg));
                    if (req_id[0] == '\0')   json_get_str(msg, "\"requestId\"", req_id, sizeof(req_id));

                    bool has_req  = req_id[0] != '\0';
                    bool has_code = err_code[0] != '\0';
                    bool not_paired = has_code && strcmp(err_code, "NOT_PAIRED") == 0;

                    ESP_LOGW(TAG, "OpenClaw: connect error code=%s msg=%s req=%s",
                             has_code ? err_code : "-",
                             err_msg[0] ? err_msg : "-",
                             has_req ? req_id : "-");

                    if (has_req || not_paired) {
                        s_auth_stage = "pairing_required";
                        strncpy(s_pair_code,
                                has_req ? req_id : "APPROVE_IN_CLI",
                                sizeof(s_pair_code) - 1);
                        s_pair_code[sizeof(s_pair_code) - 1] = '\0';
                        s_state = OPENCLAW_STATE_PAIRING;
                        openclaw_block_chat("pairing_required", 30000);
                        diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_WARN, "pairing required code=%s req=%s",
                                 has_code ? err_code : "-", has_req ? req_id : "-");
                        oc_diag_status(NULL);
                        ESP_LOGW(TAG, "OpenClaw: pairing required (code=%s req=%s)",
                                 has_code ? err_code : "-", has_req ? req_id : "-");
                        if (s_on_pair_code) s_on_pair_code(s_pair_code);
                        xEventGroupSetBits(s_evg, EVT_PAIR_CODE);
                    } else {
                        s_auth_stage = "connect_rejected";
                        s_state = OPENCLAW_STATE_DISCONNECTED;
                        openclaw_block_chat("connect_rejected", 10000);
                        diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_WARN,
                                 "connect rejected code=%s msg=%s",
                                 has_code ? err_code : "-",
                                 err_msg[0] ? err_msg : "-");
                        oc_diag_status(err_msg[0] ? err_msg : "connect_rejected");
                        ESP_LOGW(TAG, "OpenClaw: connect rejected code=%s msg=%s",
                                 has_code ? err_code : "-", err_msg[0] ? err_msg : "-");
                    }
                }
                break;
            }

            // chat.send response: capture runId
            if (json_type_is(msg, "res") && json_id_matches(msg, s_chat_req_id)) {
                bool ok = false;
                if (json_get_bool(msg, "\"ok\"", &ok) && ok) {
                    s_chat_block_until_ms = 0;
                    s_chat_block_reason[0] = '\0';
                    json_get_str(msg, "\"runId\"", s_chat_run_id, sizeof(s_chat_run_id));
                } else {
                    json_get_str(msg, "\"message\"", s_answer_buf, sizeof(s_answer_buf));
                    if (s_answer_buf[0] == '\0') {
                        strncpy(s_answer_buf, "OpenClaw chat.send failed.", sizeof(s_answer_buf) - 1);
                    }
                    if (strstr(msg, "unauthorized") || strstr(s_answer_buf, "unauthorized")) {
                        openclaw_block_chat("unauthorized", 30000);
                        diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_WARN, "chat blocked 30s unauthorized");
                    }
                    s_state = OPENCLAW_STATE_READY;
                    oc_diag_status("chat.send_failed");
                    xEventGroupSetBits(s_evg, EVT_ANSWER_READY);
                }
                break;
            }

            if (json_type_is(msg, "res") && json_id_matches(msg, s_terminal_open_req_id)) {
                bool has_error = json_top_level_has_key(msg, "error");
                if (!has_error) {
                    if (!json_get_str(msg, "\"terminalSessionId\"", s_terminal_session_id, sizeof(s_terminal_session_id)) &&
                        !json_get_str(msg, "\"sessionId\"", s_terminal_session_id, sizeof(s_terminal_session_id))) {
                        strncpy(s_terminal_session_id, "deskbot-terminal", sizeof(s_terminal_session_id) - 1);
                    }
                    s_terminal_active = true;
                    s_terminal_error[0] = '\0';
                    xEventGroupSetBits(s_evg, EVT_TERMINAL_READY);
                    openclaw_terminal_emit(OPENCLAW_TERMINAL_EVENT_READY,
                                           (const uint8_t *)s_terminal_session_id,
                                           strlen(s_terminal_session_id));
                } else {
                    if (!json_get_str(msg, "\"message\"", s_terminal_error, sizeof(s_terminal_error))) {
                        strncpy(s_terminal_error, "deskbot.session.open failed", sizeof(s_terminal_error) - 1);
                    }
                    s_terminal_active = false;
                    s_terminal_session_id[0] = '\0';
                    xEventGroupSetBits(s_evg, EVT_TERMINAL_ERROR);
                    openclaw_terminal_emit(OPENCLAW_TERMINAL_EVENT_ERROR,
                                           (const uint8_t *)s_terminal_error,
                                           strlen(s_terminal_error));
                    ESP_LOGW(TAG, "deskbot.session.open failed: %s", s_terminal_error);
                }
                break;
            }

            if (json_type_is(msg, "event") && json_event_is(msg, "deskbot.session.output_text")) {
                char text[512] = {0};
                if (!terminal_event_matches_active_session(msg)) {
                    break;
                }
                if (json_get_str(msg, "\"text\"", text, sizeof(text)) && text[0] != '\0') {
                    openclaw_terminal_emit(OPENCLAW_TERMINAL_EVENT_TEXT,
                                           (const uint8_t *)text,
                                           strlen(text));
                } else {
                    ESP_LOGW(TAG, "deskbot.session.output_text present but text extraction failed");
                }
                break;
            }

            if (json_type_is(msg, "event") && json_event_is(msg, "deskbot.session.output_audio")) {
                if (!terminal_event_matches_active_session(msg)) {
                    break;
                }
                if (json_get_str(msg, "\"data\"", s_terminal_audio_b64, sizeof(s_terminal_audio_b64)) &&
                    s_terminal_audio_b64[0] != '\0') {
                    ESP_LOGD(TAG, "deskbot.session.output_audio received b64_len=%u",
                             (unsigned)strlen(s_terminal_audio_b64));
                    openclaw_terminal_emit(OPENCLAW_TERMINAL_EVENT_AUDIO,
                                           (const uint8_t *)s_terminal_audio_b64,
                                           strlen(s_terminal_audio_b64));
                } else {
                    ESP_LOGW(TAG, "deskbot.session.output_audio present but data extraction failed (buffer=%u)",
                             (unsigned)sizeof(s_terminal_audio_b64));
                }
                break;
            }

            if (json_type_is(msg, "event") && json_event_is(msg, "deskbot.session.turn_complete")) {
                if (!terminal_event_matches_active_session(msg)) {
                    break;
                }
                openclaw_terminal_emit(OPENCLAW_TERMINAL_EVENT_TURN_COMPLETE,
                                       (const uint8_t *)"turn_complete",
                                       strlen("turn_complete"));
                break;
            }

            if (json_type_is(msg, "event") && json_event_is(msg, "deskbot.session.error")) {
                char error_msg[256] = {0};
                if (!terminal_event_matches_active_session(msg)) {
                    break;
                }
                if (!json_get_str(msg, "\"message\"", error_msg, sizeof(error_msg))) {
                    strncpy(error_msg, "deskbot.session.error", sizeof(error_msg) - 1);
                }
                ESP_LOGW(TAG, "deskbot.session.error received: %s", error_msg);
                openclaw_terminal_emit(OPENCLAW_TERMINAL_EVENT_ERROR,
                                       (const uint8_t *)error_msg,
                                       strlen(error_msg));
                break;
            }

            if (json_type_is(msg, "event") && json_event_is(msg, "deskbot.results.ready")) {
                char task_id[64] = {0};
                char summary[160] = {0};
                char payload[256] = {0};
                char ack_fields[160] = {0};

                if (json_get_str(msg, "\"taskId\"", task_id, sizeof(task_id)) &&
                    json_get_str(msg, "\"summary\"", summary, sizeof(summary)) &&
                    task_id[0] != '\0') {
                    snprintf(payload, sizeof(payload), "%s\t%s", task_id, summary);
                    openclaw_terminal_emit(OPENCLAW_TERMINAL_EVENT_NOTIFICATION_READY,
                                           (const uint8_t *)payload,
                                           strlen(payload));
                    snprintf(ack_fields, sizeof(ack_fields), "\"taskId\":\"%s\"", task_id);
                    if (!openclaw_send_device_method("deskbot.results.ack", ack_fields)) {
                        ESP_LOGW(TAG, "deskbot.results.ack send failed for task=%s", task_id);
                    }
                } else {
                    ESP_LOGW(TAG, "deskbot.results.ready present but parse failed");
                }
                break;
            }

            if (json_type_is(msg, "event") && json_event_is(msg, "deskbot.results.clear")) {
                char task_id[64] = {0};
                if (json_get_str(msg, "\"taskId\"", task_id, sizeof(task_id)) && task_id[0] != '\0') {
                    openclaw_terminal_emit(OPENCLAW_TERMINAL_EVENT_NOTIFICATION_CLEAR,
                                           (const uint8_t *)task_id,
                                           strlen(task_id));
                } else {
                    ESP_LOGW(TAG, "deskbot.results.clear present but taskId parse failed");
                }
                break;
            }

            if (json_type_is(msg, "event") && json_event_is(msg, "deskbot.results.replay")) {
                char task_id[64] = {0};
                char prompt[2048] = {0};
                char payload[2176] = {0};

                if (json_get_str(msg, "\"taskId\"", task_id, sizeof(task_id)) &&
                    json_get_str(msg, "\"prompt\"", prompt, sizeof(prompt)) &&
                    task_id[0] != '\0' &&
                    prompt[0] != '\0') {
                    snprintf(payload, sizeof(payload), "%s\t%s", task_id, prompt);
                    openclaw_terminal_emit(OPENCLAW_TERMINAL_EVENT_NOTIFICATION_REPLAY,
                                           (const uint8_t *)payload,
                                           strlen(payload));
                } else {
                    ESP_LOGW(TAG, "deskbot.results.replay present but parse failed");
                }
                break;
            }

            // chat event stream
            if (json_type_is(msg, "event") && json_event_is(msg, "chat")) {
                char run_id[64] = {0};
                char state[20] = {0};
                json_get_str(msg, "\"runId\"", run_id, sizeof(run_id));
                json_get_str(msg, "\"state\"", state, sizeof(state));
                if (s_chat_run_id[0] != '\0' && strcmp(run_id, s_chat_run_id) == 0) {
                    ESP_LOGI(TAG, "chat event received for runId=%s state=%s", run_id, state);
                    if (strcmp(state, "final") == 0) {
                        if (!json_get_str(msg, "\"text\"", s_answer_buf, sizeof(s_answer_buf))) {
                            strncpy(s_answer_buf, "OpenClaw replied (no text found).", sizeof(s_answer_buf) - 1);
                        }
                        s_state = OPENCLAW_STATE_READY;
                        oc_diag_status("chat_final");
                        xEventGroupSetBits(s_evg, EVT_ANSWER_READY);
                    } else if (strcmp(state, "error") == 0) {
                        json_get_str(msg, "\"errorMessage\"", s_answer_buf, sizeof(s_answer_buf));
                        if (s_answer_buf[0] == '\0') {
                            strncpy(s_answer_buf, "OpenClaw chat error.", sizeof(s_answer_buf) - 1);
                        }
                        s_state = OPENCLAW_STATE_READY;
                        oc_diag_status("chat_error");
                        xEventGroupSetBits(s_evg, EVT_ANSWER_READY);
                    }
                }
                break;
            }
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "OpenClaw WS error");
        s_auth_stage = "ws_error";
        s_state = OPENCLAW_STATE_DISCONNECTED;
        diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_ERROR, "ws error event");
        oc_diag_status(NULL);
        break;

    default:
        break;
    }
}

// ── Public: init ──────────────────────────────────────────────────────────────

void openclaw_init(openclaw_pair_code_cb_t on_pair_code,
                   openclaw_pair_done_cb_t on_pair_done)
{
    s_on_pair_code = on_pair_code;
    s_on_pair_done = on_pair_done;

    if (!s_evg) {
        s_evg = xEventGroupCreate();
    }

    token_load();
#if defined(OPENCLAW_DEV_TOKEN)
    // Dev token only overrides device token fallback, not gateway token.
    if (OPENCLAW_DEV_TOKEN[0] != '\0') {
        strncpy(s_token, OPENCLAW_DEV_TOKEN, sizeof(s_token) - 1);
        s_token[sizeof(s_token) - 1] = '\0';
        ESP_LOGI(TAG, "Using dev token");
    }
#endif

    init_device_id_once();
    s_auth_stage = "initialized";
    diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_INFO, "openclaw initialized");
    oc_diag_status(NULL);
    ESP_LOGI(TAG, "OpenClaw init complete (waiting for endpoint/connect)");
}

void openclaw_disconnect(void)
{
    openclaw_ws_teardown();
    s_auth_stage = "manual_disconnect";
    s_state = OPENCLAW_STATE_DISCONNECTED;
    diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_INFO, "manual disconnect");
    oc_diag_status(NULL);
}

void openclaw_start_pairing(void)
{
    // Force fresh pairing intent: remove old device token and rotate device identity.
    openclaw_disconnect();
    openclaw_forget_token();
    regenerate_device_identity();
    memset(s_pair_code, 0, sizeof(s_pair_code));
    memset(s_connect_req_id, 0, sizeof(s_connect_req_id));
    s_auth_stage = "pairing_requested";
    diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_WARN, "pairing forced with new device identity");
    oc_diag_status(NULL);
    openclaw_connect();
}

void openclaw_connect(void)
{
    if (s_ws) {
        return;
    }
    if (!oc_epoch_is_synced()) {
        uint64_t signed_at_ms = oc_epoch_ms();
        s_auth_stage = "waiting_time_sync";
        s_state = OPENCLAW_STATE_DISCONNECTED;
        openclaw_block_chat("time_sync_pending", 5000);
        diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_WARN,
                 "connect deferred until SNTP sync signedAt=%llu",
                 (unsigned long long)signed_at_ms);
        oc_diag_status("time_sync_pending");
        ESP_LOGW(TAG, "OpenClaw: connect deferred until SNTP sync (signedAt=%llu)",
                 (unsigned long long)signed_at_ms);
        return;
    }
    s_auth_stage = "connecting_ws";
    oc_diag_status(NULL);

    char uri[128];
    snprintf(uri, sizeof(uri), "ws://%s:%u%s", s_host, (unsigned)s_port, OPENCLAW_WS_PATH);

    char headers[OPENCLAW_TOKEN_MAXLEN + 64] = {0};
    const char *headers_ptr = NULL;
    // Prefer device-issued token after pairing; gateway token is bootstrap fallback.
    const char *auth_token = s_token[0] ? s_token : s_gateway_token;
    if (auth_token[0] != '\0') {
        snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", auth_token);
        headers_ptr = headers;
    }

    esp_websocket_client_config_t cfg = {
        .uri                  = uri,
        .headers              = headers_ptr,
        .reconnect_timeout_ms = 0,
        .network_timeout_ms   = 10000,
        .task_prio            = 5,
        .task_stack           = 16384,
        .buffer_size          = 1024,
        // Keep low during auth; handshake challenge can otherwise stall near 10s.
        .ping_interval_sec    = 1,
    };

    s_ws = esp_websocket_client_init(&cfg);
    if (!s_ws) {
        ESP_LOGE(TAG, "OpenClaw WS client init failed");
        s_auth_stage = "ws_init_failed";
        s_state = OPENCLAW_STATE_DISCONNECTED;
        diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_ERROR, "ws init failed");
        oc_diag_status(NULL);
        return;
    }

    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY,
                                  ws_event_handler, NULL);
    if (esp_websocket_client_start(s_ws) != ESP_OK) {
        ESP_LOGE(TAG, "OpenClaw WS start failed");
        openclaw_ws_teardown();
        s_auth_stage = "ws_start_failed";
        s_state = OPENCLAW_STATE_DISCONNECTED;
        diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_ERROR, "ws start failed");
        oc_diag_status(NULL);
        return;
    }

    s_state = OPENCLAW_STATE_CONNECTING;
    diag_log(DIAG_MODULE_OPENCLAW, DIAG_LOG_INFO, "connecting to %s:%u", s_host, (unsigned)s_port);
    oc_diag_status(NULL);
    ESP_LOGI(TAG, "OpenClaw connecting to %s (token=%s)", uri, auth_token[0] ? "set" : "EMPTY");
}

// ── Public: state ─────────────────────────────────────────────────────────────

openclaw_state_t openclaw_get_state(void)
{
    return s_state;
}

bool openclaw_is_ws_connected(void)
{
    if (!s_ws) return false;
    return esp_websocket_client_is_connected(s_ws);
}

uint32_t openclaw_get_ws_connect_attempts(void)
{
    return s_ws_connect_attempts;
}

uint32_t openclaw_get_ws_data_events(void)
{
    return s_ws_data_events;
}

uint32_t openclaw_get_ws_challenge_events(void)
{
    return s_ws_challenge_events;
}

int openclaw_get_last_ws_close_code(void)
{
    return s_last_ws_close_code;
}

int64_t openclaw_get_last_ws_connected_age_ms(void)
{
    if (s_last_ws_connected_ms <= 0) {
        return -1;
    }
    return oc_now_ms() - s_last_ws_connected_ms;
}

int64_t openclaw_get_last_challenge_age_ms(void)
{
    if (s_last_challenge_rx_ms <= 0) {
        return -1;
    }
    return oc_now_ms() - s_last_challenge_rx_ms;
}

int64_t openclaw_get_last_connect_req_age_ms(void)
{
    if (s_last_connect_req_ms <= 0) {
        return -1;
    }
    return oc_now_ms() - s_last_connect_req_ms;
}

const char *openclaw_get_auth_stage(void)
{
    return s_auth_stage;
}

int64_t openclaw_get_chat_block_remaining_ms(void)
{
    int64_t now = oc_now_ms();
    if (s_chat_block_until_ms <= now) {
        return 0;
    }
    return s_chat_block_until_ms - now;
}

const char *openclaw_get_chat_block_reason(void)
{
    return s_chat_block_reason[0] ? s_chat_block_reason : "";
}

bool openclaw_terminal_open(openclaw_terminal_event_cb_t cb, void *user_ctx)
{
    if (s_state != OPENCLAW_STATE_READY || !s_ws) {
        return false;
    }

    s_terminal_cb = cb;
    s_terminal_user_ctx = user_ctx;
    s_terminal_active = false;
    s_terminal_session_id[0] = '\0';
    s_terminal_error[0] = '\0';

    xEventGroupClearBits(s_evg, EVT_TERMINAL_READY | EVT_TERMINAL_ERROR);
    snprintf(s_terminal_open_req_id, sizeof(s_terminal_open_req_id), "oc-term-open-%lu",
             (unsigned long)s_req_seq++);

    char params[512];
    int n = snprintf(params, sizeof(params),
                     "{"
                     "\"sessionKey\":\"agent:main:deskbot\","
                     "\"deviceId\":\"%s\","
                     "\"protocol\":\"deskbot-terminal-v1\""
                     "}",
                     s_device_id);
    if (n <= 0 || n >= (int)sizeof(params)) {
        return false;
    }

    if (!openclaw_send_req_frame(s_terminal_open_req_id, "deskbot.session.open", params)) {
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(s_evg,
                                           EVT_TERMINAL_READY | EVT_TERMINAL_ERROR,
                                           pdTRUE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(7000));
    return (bits & EVT_TERMINAL_READY) != 0;
}

bool openclaw_terminal_send_audio(const uint8_t *audio_data, size_t audio_len)
{
    if (!audio_data || audio_len == 0) return false;

    size_t audio_b64_cap = ((audio_len + 2U) / 3U) * 4U + 8U;
    char *audio_b64 = malloc(audio_b64_cap);
    if (!audio_b64) {
        ESP_LOGE(TAG, "audio_b64 OOM len=%u", (unsigned)audio_len);
        return false;
    }

    size_t b64_len = 0;
    if (!openclaw_base64_encode(audio_data, audio_len, audio_b64, audio_b64_cap, &b64_len)) {
        free(audio_b64);
        return false;
    }

    size_t fields_cap = b64_len + 64U;
    char *fields = malloc(fields_cap);
    if (!fields) {
        free(audio_b64);
        ESP_LOGE(TAG, "audio fields OOM len=%u", (unsigned)b64_len);
        return false;
    }

    int n = snprintf(fields, fields_cap,
                     "\"mimeType\":\"audio/pcm;rate=16000\","
                     "\"data\":\"%.*s\"",
                     (int)b64_len, audio_b64);
    free(audio_b64);
    if (n <= 0 || n >= (int)fields_cap) {
        free(fields);
        return false;
    }

    bool ok = openclaw_terminal_send_method("deskbot.session.input_audio", fields);
    free(fields);
    return ok;
}

bool openclaw_terminal_send_text(const char *text)
{
    if (!text || text[0] == '\0') return false;

    size_t text_len = strlen(text);
    size_t escaped_cap = (text_len * 2U) + 1U;
    char *escaped = malloc(escaped_cap);
    if (!escaped) {
        ESP_LOGE(TAG, "text escape OOM len=%u", (unsigned)text_len);
        return false;
    }
    openclaw_json_escape(text, escaped, escaped_cap);

    size_t escaped_len = strlen(escaped);
    size_t fields_cap = escaped_len + 16U;
    char *fields = malloc(fields_cap);
    if (!fields) {
        ESP_LOGE(TAG, "text fields OOM len=%u", (unsigned)escaped_len);
        free(escaped);
        return false;
    }

    int n = snprintf(fields, fields_cap, "\"text\":\"%s\"", escaped);
    free(escaped);
    if (n <= 0 || n >= (int)fields_cap) {
        free(fields);
        return false;
    }

    bool ok = openclaw_terminal_send_method("deskbot.session.input_text", fields);
    free(fields);
    return ok;
}

bool openclaw_terminal_activity_start(void)
{
    return openclaw_terminal_send_method("deskbot.session.activity_start", NULL);
}

bool openclaw_terminal_activity_end(void)
{
    return openclaw_terminal_send_method("deskbot.session.activity_end", NULL);
}

void openclaw_terminal_close(void)
{
    if (s_terminal_active) {
        openclaw_terminal_send_method("deskbot.session.close", NULL);
    }
    s_terminal_active = false;
    s_terminal_session_id[0] = '\0';
    s_terminal_open_req_id[0] = '\0';
    s_terminal_error[0] = '\0';
    s_terminal_cb = NULL;
    s_terminal_user_ctx = NULL;
}

bool openclaw_terminal_is_active(void)
{
    return s_terminal_active;
}

// ── Public: ask ───────────────────────────────────────────────────────────────

bool openclaw_ask(const char *question, char *answer_buf, size_t answer_buf_sz)
{
    if (!question || !answer_buf || answer_buf_sz < 8) return false;

    int64_t block_ms = openclaw_get_chat_block_remaining_ms();
    if (block_ms > 0) {
        int64_t sec = (block_ms + 999) / 1000;
        snprintf(answer_buf, answer_buf_sz,
                 "Jerry access is temporarily blocked (%s). Try again in %llds.",
                 openclaw_get_chat_block_reason(),
                 (long long)sec);
        return false;
    }

    if (s_state != OPENCLAW_STATE_READY) {
        ESP_LOGW(TAG, "openclaw_ask called but state=%d (not READY)", (int)s_state);
        snprintf(answer_buf, answer_buf_sz,
                 "Jerry is not available right now.");
        return false;
    }

    // Clear any stale answer event
    xEventGroupClearBits(s_evg, EVT_ANSWER_READY);
    s_answer_buf[0] = '\0';
    s_chat_run_id[0] = '\0';
    s_state = OPENCLAW_STATE_BUSY;
    oc_diag_status("chat_busy");

    // Use heap for large buffers to prevent stack overflow
    char *escaped_q = malloc(4096);
    if (!escaped_q) return false;
    openclaw_json_escape(question, escaped_q, 4096);

    // Ensure idempotency key is truly unique across reboots to prevent
    // the Gateway from instantly returning cached results from a prior session.
    static uint32_t s_req_session_id = 0;
    if (s_req_session_id == 0) {
        s_req_session_id = esp_random();
    }
    snprintf(s_chat_req_id, sizeof(s_chat_req_id), "oc-chat-%08x-%lu", 
             (unsigned)s_req_session_id, (unsigned long)s_req_seq++);

    char *params = malloc(5120);
    if (!params) { free(escaped_q); return false; }
    
    int n = snprintf(params,
                     5120,
                     "{"
                     "\"sessionKey\":\"deskbot\","
                     "\"message\":\"%s\","
                     "\"deliver\":false,"
                     "\"idempotencyKey\":\"%s\""
                     "}",
                     escaped_q,
                     s_chat_req_id);
    if (n <= 0 || n >= 5120) {
        ESP_LOGE(TAG, "chat.send params too long");
        free(escaped_q);
        free(params);
        s_state = OPENCLAW_STATE_READY;
        oc_diag_status("chat_send_error");
        snprintf(answer_buf, answer_buf_sz, "I could not reach Jerry.");
        return false;
    }

    if (!openclaw_send_req_frame(s_chat_req_id, "chat.send", params)) {
        ESP_LOGE(TAG, "chat.send frame send failed");
        free(escaped_q);
        free(params);
        s_state = OPENCLAW_STATE_READY;
        snprintf(answer_buf, answer_buf_sz, "I could not reach Jerry.");
        return false;
    }
    
    free(escaped_q);
    free(params);

    ESP_LOGI(TAG, "chat.send sent id=%s, waiting for answer (timeout %dms)...",
             s_chat_req_id, OPENCLAW_TIMEOUT_MS);

    // Block until EVT_ANSWER_READY or timeout
    EventBits_t bits = xEventGroupWaitBits(s_evg, EVT_ANSWER_READY,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(OPENCLAW_TIMEOUT_MS));
    if (!(bits & EVT_ANSWER_READY)) {
        ESP_LOGW(TAG, "openclaw_ask timed out");
        s_state = OPENCLAW_STATE_READY;
        oc_diag_status("chat_timeout");
        snprintf(answer_buf, answer_buf_sz,
                 "Jerry did not respond in time.");
        return false;
    }

    // Copy answer out
    strncpy(answer_buf, s_answer_buf, answer_buf_sz - 1);
    answer_buf[answer_buf_sz - 1] = '\0';
    s_state = OPENCLAW_STATE_READY;
    oc_diag_status(NULL);
    return true;
}
