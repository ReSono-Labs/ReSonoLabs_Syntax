#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── OpenClaw gateway configuration ───────────────────────────────────────────
// Single endpoint. Uses mDNS name so no IP hardcoding needed on the LAN.
// Change OPENCLAW_HOST to an IP string if mDNS is unavailable.
#define OPENCLAW_HOST        "192.168.1.229"
#define OPENCLAW_PORT        18789
#define OPENCLAW_WS_PATH     "/"
#define OPENCLAW_TIMEOUT_MS  20000   // max wait for an answer (ms)
#define OPENCLAW_TOKEN_MAXLEN 256    // NVS storage limit for the bearer token

// Optional dev token for bring-up.
// Keep this empty to use the NVS-stored token from pairing.
#define OPENCLAW_DEV_TOKEN   ""

// ── Connection / pairing state ────────────────────────────────────────────────
typedef enum {
    OPENCLAW_STATE_DISCONNECTED = 0,
    OPENCLAW_STATE_CONNECTING,
    OPENCLAW_STATE_PAIRING,      // waiting for user to approve on OpenClaw app
    OPENCLAW_STATE_READY,        // paired and connected
    OPENCLAW_STATE_BUSY,         // waiting for answer to in-flight query
} openclaw_state_t;

// ── Callbacks provided by the application ────────────────────────────────────
// Called when the gateway sends a pairing code to show to the user.
typedef void (*openclaw_pair_code_cb_t)(const char *code);
// Called when pairing completes (token saved, will reconnect as READY).
typedef void (*openclaw_pair_done_cb_t)(void);

typedef enum {
    OPENCLAW_TERMINAL_EVENT_READY = 0,
    OPENCLAW_TERMINAL_EVENT_TEXT,
    OPENCLAW_TERMINAL_EVENT_AUDIO,
    OPENCLAW_TERMINAL_EVENT_TURN_COMPLETE,
    OPENCLAW_TERMINAL_EVENT_ERROR,
    OPENCLAW_TERMINAL_EVENT_NOTIFICATION_READY,
    OPENCLAW_TERMINAL_EVENT_NOTIFICATION_CLEAR,
    OPENCLAW_TERMINAL_EVENT_NOTIFICATION_REPLAY,
} openclaw_terminal_event_type_t;

typedef void (*openclaw_terminal_event_cb_t)(openclaw_terminal_event_type_t type,
                                             const uint8_t *data,
                                             size_t data_len,
                                             void *user_ctx);

// ── Public API ────────────────────────────────────────────────────────────────

/**
 * Initialise the OpenClaw client. Call once after WiFi is connected.
 * Loads any saved token from NVS and opens the WebSocket connection.
 */
void openclaw_init(openclaw_pair_code_cb_t on_pair_code,
                   openclaw_pair_done_cb_t on_pair_done);

/**
 * Set runtime endpoint used for the OpenClaw websocket.
 */
bool openclaw_set_endpoint(const char *host, uint16_t port);

/**
 * Connect/disconnect OpenClaw websocket using current endpoint and token.
 */
void openclaw_connect(void);
void openclaw_disconnect(void);
void openclaw_start_pairing(void);

/**
 * Returns the current connection/pairing state.
 */
openclaw_state_t openclaw_get_state(void);
bool openclaw_is_ws_connected(void);

/**
 * Send a question to Jerry and block until an answer arrives or timeout.
 * Must only be called when openclaw_get_state() == OPENCLAW_STATE_READY.
 *
 * @param question      Null-terminated question string.
 * @param answer_buf    Caller buffer for the response text.
 * @param answer_buf_sz Size of answer_buf in bytes.
 * @return true  if an answer was received.
 * @return false on timeout or error (answer_buf holds a fallback message).
 */
bool openclaw_ask(const char *question, char *answer_buf, size_t answer_buf_sz);

/**
 * Escape a plain string for safe embedding inside a JSON string value.
 */
void openclaw_json_escape(const char *src, char *dst, size_t dst_sz);

/**
 * Erase the saved pairing token from NVS (forces re-pairing on next boot).
 */
void openclaw_forget_token(void);
void openclaw_set_gateway_token(const char *token);
bool openclaw_has_gateway_token(void);

/**
 * Read-only connection metadata for portal/status UI.
 */
bool openclaw_has_token(void);
const char *openclaw_get_endpoint_host(void);
uint16_t openclaw_get_endpoint_port(void);
uint32_t openclaw_get_ws_connect_attempts(void);
uint32_t openclaw_get_ws_data_events(void);
uint32_t openclaw_get_ws_challenge_events(void);
int openclaw_get_last_ws_close_code(void);
int64_t openclaw_get_last_ws_connected_age_ms(void);
int64_t openclaw_get_last_challenge_age_ms(void);
int64_t openclaw_get_last_connect_req_age_ms(void);
const char *openclaw_get_auth_stage(void);
int64_t openclaw_get_chat_block_remaining_ms(void);
const char *openclaw_get_chat_block_reason(void);

/**
 * DeskBot terminal-mode session helpers.
 * These target the planned plugin RPC surface and keep firmware transport logic
 * local to the existing OpenClaw websocket client.
 */
bool openclaw_terminal_open(openclaw_terminal_event_cb_t cb, void *user_ctx);
bool openclaw_terminal_register_listener(openclaw_terminal_event_cb_t cb, void *user_ctx);
bool openclaw_terminal_send_audio(const uint8_t *audio_data, size_t audio_len);
bool openclaw_terminal_send_text(const char *text);
bool openclaw_terminal_activity_start(void);
bool openclaw_terminal_activity_end(void);
bool openclaw_results_replay(const char *task_id);
bool openclaw_results_consume(const char *task_id);
void openclaw_terminal_close(void);
bool openclaw_terminal_is_active(void);

#ifdef __cplusplus
}
#endif
