#include "app.h"

#include <stddef.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "board_registry.h"
#include "audio_platform.h"
#include "display_platform.h"
#include "input_platform.h"
#include "network_platform.h"
#include "orb_service.h"
#include "ota_service.h"
#include "power_platform.h"
#include "provider_runtime_api.h"
#include "provider_transport.h"
#include "provider_state_bridge.h"
#include "provider_web_bridge.h"
#include "security_auth.h"
#include "settings_service.h"
#include "state.h"
#include "storage_platform.h"
#include "ui_shell.h"
#include "web_server_platform.h"
#include "web_shell.h"
#include "wifi_provisioning.h"
#include "wifi_setup_service.h"

typedef struct {
    const board_profile_t *board;
    bool bootstrapped;
    bool mic_streaming;
    bool ptt_active;
    bool terminal_open_in_flight;
    int64_t terminal_initialize_deadline_ms;
    bool mic_drain_pending;
    bool mic_stop_finalize_pending;
    bool turn_complete_pending;
    int64_t mic_drain_deadline_ms;
    size_t mic_ring_rd;
    size_t mic_ring_wr;
    int64_t state_since_ms;
    int64_t last_audio_rx_ms;
    int64_t mic_log_ms;
    uint32_t mic_cb_calls;
    uint32_t mic_tx_frames;
    size_t mic_cb_bytes;
    size_t mic_tx_bytes;
    uint32_t mic_send_failures;
    bool replay_send_pending;
    int64_t replay_send_not_before_ms;
    bool replay_request_pending;
    int64_t replay_request_not_before_ms;
    int64_t terminal_auto_open_not_before_ms;
    char replay_send_task_id[64];
    char pending_replay_task_id[64];
    char pending_replay_prompt[2048];
    TaskHandle_t terminal_open_task;
    TaskHandle_t mic_stream_task;
    bool provisioning_only;
} app_context_t;

static app_context_t s_app;
static const char *TAG = "app";
static EXT_RAM_BSS_ATTR uint8_t s_mic_ring[16384];
static uint8_t s_mic_pop_buf[1536];
static EXT_RAM_BSS_ATTR uint8_t s_audio_decode_buf[16384];
static portMUX_TYPE s_mic_ring_lock = portMUX_INITIALIZER_UNLOCKED;
#define APP_MIC_RING_HIGH_WATER 1024U
#define APP_MIC_SEND_CHUNK_MAX 480U
#define APP_MIC_SEND_FAILURE_LIMIT 3U

static void app_parse_notification_payload(const uint8_t *data,
                                           size_t data_len,
                                           char *out_task_id,
                                           size_t out_task_id_len,
                                           char *out_summary,
                                           size_t out_summary_len)
{
    size_t copy_len;
    char payload[256];
    char *sep;

    if (out_task_id != NULL && out_task_id_len > 0) {
        out_task_id[0] = '\0';
    }
    if (out_summary != NULL && out_summary_len > 0) {
        out_summary[0] = '\0';
    }
    if (data == NULL || data_len == 0 || out_task_id == NULL || out_task_id_len == 0) {
        return;
    }

    copy_len = data_len < (sizeof(payload) - 1U) ? data_len : (sizeof(payload) - 1U);
    memcpy(payload, data, copy_len);
    payload[copy_len] = '\0';
    sep = strchr(payload, '\t');
    if (sep != NULL) {
        *sep = '\0';
        ++sep;
        if (out_summary != NULL && out_summary_len > 0) {
            strncpy(out_summary, sep, out_summary_len - 1U);
            out_summary[out_summary_len - 1U] = '\0';
        }
    }

    strncpy(out_task_id, payload, out_task_id_len - 1U);
    out_task_id[out_task_id_len - 1U] = '\0';
}

static void app_parse_replay_payload(const uint8_t *data,
                                     size_t data_len,
                                     char *out_task_id,
                                     size_t out_task_id_len,
                                     char *out_prompt,
                                     size_t out_prompt_len)
{
    app_parse_notification_payload(data, data_len, out_task_id, out_task_id_len, out_prompt, out_prompt_len);
}

static void app_terminal_open_task(void *ctx);
static bool app_start_listening_session(void);

static bool app_terminal_error_is_benign(const uint8_t *data, size_t data_len)
{
    size_t copy_len;
    char error_msg[256];

    if (data == NULL || data_len == 0) {
        return false;
    }

    copy_len = data_len < (sizeof(error_msg) - 1U) ? data_len : (sizeof(error_msg) - 1U);
    memcpy(error_msg, data, copy_len);
    error_msg[copy_len] = '\0';

    return strstr(error_msg, "openclaw_ws_disconnected") != NULL ||
           strstr(error_msg, "closed (1000)") != NULL ||
           strstr(error_msg, "closed (1011: Deadline expired before operation could complete.)") != NULL ||
           strstr(error_msg, "Deadline expired before operation could complete.") != NULL;
}

static size_t app_mic_ring_available(void)
{
    size_t avail;

    portENTER_CRITICAL(&s_mic_ring_lock);
    if (s_app.mic_ring_wr >= s_app.mic_ring_rd) {
        avail = s_app.mic_ring_wr - s_app.mic_ring_rd;
    } else {
        avail = sizeof(s_mic_ring) - s_app.mic_ring_rd + s_app.mic_ring_wr;
    }
    portEXIT_CRITICAL(&s_mic_ring_lock);

    return avail;
}

static void app_mic_ring_reset(void)
{
    portENTER_CRITICAL(&s_mic_ring_lock);
    s_app.mic_ring_rd = 0;
    s_app.mic_ring_wr = 0;
    portEXIT_CRITICAL(&s_mic_ring_lock);
}

static size_t app_mic_ring_push(const uint8_t *data, size_t len)
{
    size_t pushed = 0;
    size_t space;

    if (data == NULL || len == 0) {
        return 0;
    }

    portENTER_CRITICAL(&s_mic_ring_lock);
    if (s_app.mic_ring_wr >= s_app.mic_ring_rd) {
        space = (sizeof(s_mic_ring) - 1U) - (s_app.mic_ring_wr - s_app.mic_ring_rd);
    } else {
        space = (sizeof(s_mic_ring) - 1U) - (sizeof(s_mic_ring) - s_app.mic_ring_rd + s_app.mic_ring_wr);
    }
    if (space == 0) {
        portEXIT_CRITICAL(&s_mic_ring_lock);
        return 0;
    }
    if (len > space) {
        len = space;
    }

    while (pushed < len) {
        s_mic_ring[s_app.mic_ring_wr] = data[pushed++];
        s_app.mic_ring_wr = (s_app.mic_ring_wr + 1U) % sizeof(s_mic_ring);
    }
    portEXIT_CRITICAL(&s_mic_ring_lock);

    return pushed;
}

static size_t app_mic_ring_pop(uint8_t *dst, size_t len)
{
    size_t popped = 0;
    size_t avail;

    if (dst == NULL || len == 0) {
        return 0;
    }

    portENTER_CRITICAL(&s_mic_ring_lock);
    if (s_app.mic_ring_wr >= s_app.mic_ring_rd) {
        avail = s_app.mic_ring_wr - s_app.mic_ring_rd;
    } else {
        avail = sizeof(s_mic_ring) - s_app.mic_ring_rd + s_app.mic_ring_wr;
    }
    if (avail == 0) {
        portEXIT_CRITICAL(&s_mic_ring_lock);
        return 0;
    }
    if (len > avail) {
        len = avail;
    }

    while (popped < len) {
        dst[popped++] = s_mic_ring[s_app.mic_ring_rd];
        s_app.mic_ring_rd = (s_app.mic_ring_rd + 1U) % sizeof(s_mic_ring);
    }
    portEXIT_CRITICAL(&s_mic_ring_lock);

    return popped;
}

static void app_stop_mic_capture(void)
{
    if (s_app.mic_streaming) {
        audio_platform_stop_mic();
        s_app.mic_streaming = false;
    }
}

static int64_t app_now_ms(void)
{
    return (int64_t)(esp_timer_get_time() / 1000ULL);
}


static void app_queue_replay_request_inplace(void)
{
    if (s_app.pending_replay_task_id[0] == '\0' || s_app.pending_replay_prompt[0] == '\0') {
        return;
    }
    s_app.replay_request_pending = true;
    s_app.replay_request_not_before_ms = app_now_ms() + 150LL;
}

static void app_queue_replay_send(const char *task_id)
{
    if (task_id == NULL || task_id[0] == '\0') {
        return;
    }
    strncpy(s_app.replay_send_task_id, task_id, sizeof(s_app.replay_send_task_id) - 1U);
    s_app.replay_send_task_id[sizeof(s_app.replay_send_task_id) - 1U] = '\0';
    s_app.replay_send_pending = true;
    s_app.replay_send_not_before_ms = app_now_ms() + 100LL;
}

static void app_process_replay_send(void)
{
    if (!s_app.replay_send_pending || app_now_ms() < s_app.replay_send_not_before_ms) {
        return;
    }
    if (s_app.replay_send_task_id[0] == '\0') {
        s_app.replay_send_pending = false;
        return;
    }
    if (!provider_runtime_results_replay(s_app.replay_send_task_id)) {
        ESP_LOGW(TAG, "Notification replay request deferred id=%s", s_app.replay_send_task_id);
        s_app.replay_send_not_before_ms = app_now_ms() + 300LL;
        return;
    }
    s_app.replay_send_pending = false;
    s_app.replay_send_task_id[0] = '\0';
}

static void app_process_replay_request(void)
{
    runtime_snapshot_t runtime;
    provider_terminal_snapshot_t terminal;

    if (!s_app.replay_request_pending || app_now_ms() < s_app.replay_request_not_before_ms) {
        return;
    }
    if (s_app.pending_replay_task_id[0] == '\0' || s_app.pending_replay_prompt[0] == '\0') {
        s_app.replay_request_pending = false;
        return;
    }
    memset(&runtime, 0, sizeof(runtime));
    if (!provider_runtime_get_snapshot(&runtime) || runtime.status != RUNTIME_STATUS_READY) {
        s_app.replay_request_not_before_ms = app_now_ms() + 250LL;
        return;
    }
    memset(&terminal, 0, sizeof(terminal));
    if (!provider_runtime_terminal_get_snapshot(&terminal)) {
        ESP_LOGW(TAG, "Replay deferred: terminal snapshot unavailable");
        s_app.replay_request_not_before_ms = app_now_ms() + 250LL;
        return;
    }
    if (!terminal.session_open) {
        if (!provider_runtime_terminal_open()) {
            ESP_LOGW(TAG, "Replay deferred: terminal open failed");
            s_app.replay_request_not_before_ms = app_now_ms() + 250LL;
            return;
        }
    }
    if (!terminal.ready && terminal.session_open) {
        s_app.replay_request_not_before_ms = app_now_ms() + 150LL;
        return;
    }
    if (!provider_runtime_terminal_send_text(s_app.pending_replay_prompt)) {
        ESP_LOGW(TAG, "Replay deferred: terminal send failed task=%s", s_app.pending_replay_task_id);
        provider_runtime_terminal_close();
        s_app.replay_request_not_before_ms = app_now_ms() + 250LL;
        return;
    }
    s_app.replay_request_pending = false;
    s_app.pending_replay_prompt[0] = '\0';
    app_state_set(APP_STATE_THINKING);
}

static void app_on_terminal_event(provider_transport_terminal_event_type_t type,
                                  const uint8_t *data,
                                  size_t data_len,
                                  void *ctx)
{
    (void)ctx;
    char task_id[64];
    char summary[96];
    switch (type) {
    case PROVIDER_TRANSPORT_TERMINAL_EVENT_READY:
        if (app_state_get() == APP_STATE_INITIALIZING) {
            s_app.terminal_initialize_deadline_ms = 0;
            s_app.terminal_open_in_flight = false;
            if (!app_start_listening_session()) {
                app_state_set(APP_STATE_IDLE);
            }
        } else if (app_state_get() == APP_STATE_CONNECTING ||
            app_state_get() == APP_STATE_BOOT ||
            app_state_get() == APP_STATE_ERROR) {
            app_state_set(APP_STATE_IDLE);
        }
        break;
    case PROVIDER_TRANSPORT_TERMINAL_EVENT_TEXT:
        if (app_state_get() == APP_STATE_LISTENING || app_state_get() == APP_STATE_THINKING) {
            app_state_set(APP_STATE_THINKING);
        }
        break;
    case PROVIDER_TRANSPORT_TERMINAL_EVENT_AUDIO:
        s_app.last_audio_rx_ms = app_now_ms();
        if (data != NULL && data_len > 0) {
        size_t out_len = 0;

            if (mbedtls_base64_decode(s_audio_decode_buf,
                                      sizeof(s_audio_decode_buf),
                                      &out_len,
                                      data,
                                      data_len) == 0 &&
                out_len > 0) {
                audio_platform_write_speaker(s_audio_decode_buf, out_len);
            } else {
                ESP_LOGW(TAG, "terminal audio decode failed len=%u", (unsigned)data_len);
            }
        }
        if (app_state_get() == APP_STATE_THINKING || app_state_get() == APP_STATE_LISTENING) {
            app_state_set(APP_STATE_TALKING);
        }
        break;
    case PROVIDER_TRANSPORT_TERMINAL_EVENT_TURN_COMPLETE:
        s_app.ptt_active = false;
        s_app.terminal_open_in_flight = false;
        s_app.terminal_initialize_deadline_ms = 0;
        s_app.mic_drain_pending = false;
        s_app.mic_stop_finalize_pending = false;
        s_app.mic_drain_deadline_ms = 0;
        app_stop_mic_capture();
        app_mic_ring_reset();
        audio_platform_finish_speaker();
        if (s_app.pending_replay_task_id[0] != '\0') {
            if (!provider_runtime_results_consume(s_app.pending_replay_task_id)) {
                ESP_LOGW(TAG, "Replay consume failed task=%s", s_app.pending_replay_task_id);
            }
            s_app.pending_replay_task_id[0] = '\0';
        }
        if (app_state_get() == APP_STATE_TALKING) {
            s_app.turn_complete_pending = true;
        } else {
            s_app.turn_complete_pending = false;
            app_state_set(APP_STATE_IDLE);
        }
        break;
    case PROVIDER_TRANSPORT_TERMINAL_EVENT_ERROR:
        if (app_terminal_error_is_benign(data, data_len)) {
            s_app.ptt_active = false;
            s_app.mic_drain_pending = false;
            s_app.mic_stop_finalize_pending = false;
            s_app.turn_complete_pending = false;
            s_app.mic_drain_deadline_ms = 0;
            app_stop_mic_capture();
            app_mic_ring_reset();
            if (app_state_get() != APP_STATE_IDLE) {
                app_state_set(APP_STATE_IDLE);
            }
            break;
        }
        s_app.ptt_active = false;
        s_app.mic_drain_pending = false;
        s_app.mic_stop_finalize_pending = false;
        s_app.turn_complete_pending = false;
        s_app.mic_drain_deadline_ms = 0;
        app_stop_mic_capture();
        app_mic_ring_reset();
        app_state_set(APP_STATE_ERROR);
        break;
    case PROVIDER_TRANSPORT_TERMINAL_EVENT_NOTIFICATION_READY:
        memset(task_id, 0, sizeof(task_id));
        memset(summary, 0, sizeof(summary));
        app_parse_notification_payload(data, data_len, task_id, sizeof(task_id), summary, sizeof(summary));
        if (task_id[0] != '\0' && summary[0] != '\0') {
            ESP_LOGI(TAG, "Notification ready task=%s summary=%s", task_id, summary);
            ui_shell_notification_upsert(task_id, summary);
        } else {
            ESP_LOGW(TAG, "Notification ready parse failed len=%u", (unsigned)data_len);
        }
        break;
    case PROVIDER_TRANSPORT_TERMINAL_EVENT_NOTIFICATION_CLEAR:
        memset(task_id, 0, sizeof(task_id));
        app_parse_notification_payload(data, data_len, task_id, sizeof(task_id), NULL, 0);
        if (task_id[0] != '\0') {
            ESP_LOGI(TAG, "Notification clear task=%s", task_id);
            ui_shell_notification_remove(task_id);
        } else {
            ESP_LOGW(TAG, "Notification clear parse failed len=%u", (unsigned)data_len);
        }
        break;
    case PROVIDER_TRANSPORT_TERMINAL_EVENT_NOTIFICATION_REPLAY:
        memset(s_app.pending_replay_task_id, 0, sizeof(s_app.pending_replay_task_id));
        memset(s_app.pending_replay_prompt, 0, sizeof(s_app.pending_replay_prompt));
        app_parse_replay_payload(data,
                                 data_len,
                                 s_app.pending_replay_task_id,
                                 sizeof(s_app.pending_replay_task_id),
                                 s_app.pending_replay_prompt,
                                 sizeof(s_app.pending_replay_prompt));
        if (s_app.pending_replay_task_id[0] != '\0' && s_app.pending_replay_prompt[0] != '\0') {
            app_queue_replay_request_inplace();
        } else {
            ESP_LOGW(TAG, "Notification replay parse failed len=%u", (unsigned)data_len);
        }
        break;
    default:
        break;
    }
}

static void app_process_mic_stream(void)
{
    size_t avail;
    size_t to_pop;
    size_t got;

    if (!s_app.ptt_active && !s_app.mic_drain_pending) {
        if (app_mic_ring_available() > 0) {
            app_mic_ring_reset();
        }
        return;
    }

    avail = app_mic_ring_available();
    if (s_app.mic_drain_pending && avail < 6U) {
        if (avail > 0) {
            app_mic_ring_reset();
        }
        app_stop_mic_capture();
        s_app.ptt_active = false;
        s_app.mic_drain_pending = false;
        s_app.mic_drain_deadline_ms = 0;
        s_app.mic_stop_finalize_pending = true;
        return;
    }

    if (avail < 6U) {
        return;
    }

    to_pop = avail < APP_MIC_SEND_CHUNK_MAX ? avail : APP_MIC_SEND_CHUNK_MAX;
    to_pop = (to_pop / 6U) * 6U;
    if (to_pop < 6U) {
        return;
    }

    got = app_mic_ring_pop(s_mic_pop_buf, to_pop);
    if (got >= 2U) {
        if (!provider_runtime_terminal_send_audio(s_mic_pop_buf, got)) {
            ESP_LOGW(TAG, "terminal audio send failed bytes=%u", (unsigned)got);
            s_app.mic_send_failures++;
            if (s_app.mic_send_failures >= APP_MIC_SEND_FAILURE_LIMIT) {
                ESP_LOGW(TAG, "terminal audio send failure limit hit -> closing terminal");
                s_app.ptt_active = false;
                s_app.mic_drain_pending = false;
                s_app.mic_stop_finalize_pending = false;
                s_app.turn_complete_pending = false;
                s_app.mic_drain_deadline_ms = 0;
                app_stop_mic_capture();
                app_mic_ring_reset();
                provider_runtime_terminal_close();
                s_app.terminal_open_in_flight = false;
                s_app.terminal_initialize_deadline_ms = 0;
                app_state_set(APP_STATE_IDLE);
            }
        } else {
            s_app.mic_send_failures = 0;
            s_app.mic_tx_frames++;
            s_app.mic_tx_bytes += got;
        }
    }
}

static void app_mic_stream_task(void *ctx)
{
    (void)ctx;

    while (true) {
        app_process_mic_stream();
        if (s_app.ptt_active || s_app.mic_drain_pending) {
            vTaskDelay(pdMS_TO_TICKS(5));
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

static void app_on_state_changed(app_state_t state, void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG, "State changed -> %d", (int)state);
    s_app.state_since_ms = app_now_ms();
    if (state != APP_STATE_LISTENING && !s_app.mic_drain_pending && s_app.mic_streaming) {
        app_stop_mic_capture();
    }
    ui_shell_sync_from_state(state);
}

static void app_on_mic_audio(const uint8_t *pcm, size_t len, void *ctx)
{
    size_t avail_before;
    size_t pushed;

    (void)ctx;

    if (pcm == NULL || len == 0 || !s_app.mic_streaming || !s_app.ptt_active) {
        return;
    }

    s_app.mic_cb_calls++;
    s_app.mic_cb_bytes += len;
    avail_before = app_mic_ring_available();
    if (avail_before >= APP_MIC_RING_HIGH_WATER) {
        /* Keep latency bounded under transport pressure by dropping stale
         * backlog instead of preserving old mic audio until the websocket
         * falls over. */
        app_mic_ring_reset();
        avail_before = 0;
    }
    pushed = app_mic_ring_push(pcm, len);
    if (pushed != len) {
        ESP_LOGW(TAG, "mic ring overflow dropped=%u", (unsigned)(len - pushed));
    }
}

static void app_on_input_event(const input_event_t *event, void *ctx)
{
    (void)ctx;
    if (event != NULL) {
        ESP_LOGI(TAG,
                 "Input event type=%d x=%d y=%d state=%d ptt=%d mic=%d drain=%d",
                 (int)event->type,
                 (int)event->x,
                 (int)event->y,
                 (int)app_state_get(),
                 s_app.ptt_active ? 1 : 0,
                 s_app.mic_streaming ? 1 : 0,
                 s_app.mic_drain_pending ? 1 : 0);
    }
    ui_shell_handle_input_event(event);
}

static void app_on_idle_tap(void *ctx)
{
    provider_terminal_snapshot_t terminal;

    (void)ctx;
    memset(&terminal, 0, sizeof(terminal));

    if (!provider_runtime_terminal_get_snapshot(&terminal)) {
        ESP_LOGW(TAG, "Idle tap ignored: terminal snapshot unavailable");
        return;
    }
    ESP_LOGI(TAG,
             "Idle tap session_open=%d ready=%d mic_active=%d turn=%d",
             terminal.session_open ? 1 : 0,
             terminal.ready ? 1 : 0,
             terminal.mic_active ? 1 : 0,
             terminal.turn_in_progress ? 1 : 0);

    if (terminal.session_open &&
        (terminal.turn_in_progress || terminal.mic_active || !terminal.ready)) {
        ESP_LOGW(TAG,
                 "Idle tap forcing terminal reset session_open=%d ready=%d mic_active=%d turn=%d",
                 terminal.session_open ? 1 : 0,
                 terminal.ready ? 1 : 0,
                 terminal.mic_active ? 1 : 0,
                 terminal.turn_in_progress ? 1 : 0);
        provider_runtime_terminal_close();
        vTaskDelay(pdMS_TO_TICKS(100));
        memset(&terminal, 0, sizeof(terminal));
    }

    if (s_app.terminal_open_in_flight) {
        ESP_LOGW(TAG, "Idle tap ignored: terminal open already in flight");
        return;
    }

    s_app.terminal_open_in_flight = true;
    s_app.terminal_initialize_deadline_ms = app_now_ms() + 12000LL;
    app_state_set(APP_STATE_INITIALIZING);
    if (s_app.terminal_open_task == NULL) {
        s_app.terminal_open_in_flight = false;
        ESP_LOGW(TAG, "terminal open worker unavailable");
        app_state_set(APP_STATE_IDLE);
        return;
    }

    xTaskNotifyGive(s_app.terminal_open_task);
}

static void app_terminal_open_task(void *ctx)
{
    (void)ctx;

    while (true) {
        provider_terminal_snapshot_t terminal;

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        memset(&terminal, 0, sizeof(terminal));

        if (!provider_runtime_terminal_get_snapshot(&terminal)) {
            ESP_LOGW(TAG, "terminal open task: snapshot unavailable");
            s_app.terminal_open_in_flight = false;
            s_app.terminal_initialize_deadline_ms = 0;
            app_state_set(APP_STATE_IDLE);
            continue;
        }

        if (!terminal.session_open) {
            if (!provider_runtime_terminal_open()) {
                ESP_LOGW(TAG, "terminal open task: terminal open failed");
                s_app.terminal_open_in_flight = false;
                continue;
            }
        }

        if (terminal.session_open && terminal.ready && !app_start_listening_session()) {
            s_app.terminal_open_in_flight = false;
            s_app.terminal_initialize_deadline_ms = 0;
            app_state_set(APP_STATE_IDLE);
        }
    }
}

static bool app_start_listening_session(void)
{
    if (s_app.ptt_active || s_app.mic_streaming || app_state_get() == APP_STATE_LISTENING) {
        return true;
    }

    if (!provider_runtime_terminal_start_mic()) {
        ESP_LOGW(TAG, "terminal start mic failed");
        return false;
    }

    app_mic_ring_reset();
    audio_platform_clear_speaker_buffer();
    s_app.ptt_active = true;
    s_app.mic_drain_pending = false;
    s_app.mic_drain_deadline_ms = 0;
    s_app.turn_complete_pending = false;
    s_app.last_audio_rx_ms = 0;
    s_app.mic_log_ms = app_now_ms();
    s_app.mic_cb_calls = 0;
    s_app.mic_tx_frames = 0;
    s_app.mic_cb_bytes = 0;
    s_app.mic_tx_bytes = 0;
    s_app.mic_send_failures = 0;
    if (!audio_platform_start_mic(app_on_mic_audio, NULL)) {
        ESP_LOGW(TAG, "audio_platform_start_mic failed");
        provider_runtime_terminal_stop_mic();
        return false;
    }

    s_app.mic_streaming = true;
    s_app.terminal_open_in_flight = false;
    s_app.terminal_initialize_deadline_ms = 0;
    ESP_LOGI(TAG, "Idle tap -> LISTENING");
    app_state_set(APP_STATE_LISTENING);
    return true;
}

static void app_on_listening_tap(void *ctx)
{
    (void)ctx;
    ESP_LOGI(TAG,
             "Listening tap ptt=%d mic=%d drain=%d",
             s_app.ptt_active ? 1 : 0,
             s_app.mic_streaming ? 1 : 0,
             s_app.mic_drain_pending ? 1 : 0);
    if (s_app.ptt_active || s_app.mic_streaming) {
        s_app.ptt_active = false;
        app_stop_mic_capture();
        s_app.mic_drain_pending = app_mic_ring_available() >= 6U;
        if (s_app.mic_drain_pending) {
            s_app.mic_drain_deadline_ms = app_now_ms() + 1200LL;
            s_app.mic_stop_finalize_pending = false;
        } else {
            s_app.mic_drain_deadline_ms = 0;
            app_mic_ring_reset();
            s_app.mic_stop_finalize_pending = true;
        }
        ESP_LOGI(TAG, "Listening tap -> stop mic and finalize");
    }
}

static void app_on_error_tap(void *ctx)
{
    (void)ctx;
    provider_state_bridge_handle_error_tap();
}

static void app_on_notification_selected(const char *notification_id, void *ctx)
{
    (void)ctx;
    if (notification_id == NULL || notification_id[0] == '\0') {
        return;
    }
    app_queue_replay_send(notification_id);
}

static void app_on_volume_changed(int volume, void *ctx)
{
    (void)ctx;

    if (volume < 0) {
        volume = 0;
    } else if (volume > 100) {
        volume = 100;
    }

    audio_platform_set_volume((uint8_t)volume);
    ESP_LOGI(TAG, "Volume changed -> %d", volume);
}

static void app_process_turn_state(void)
{
    int64_t now_ms = app_now_ms();
    app_state_t state = app_state_get();

    if (state == APP_STATE_THINKING &&
        !s_app.ptt_active &&
        !s_app.mic_drain_pending &&
        (now_ms - s_app.state_since_ms) > 15000LL) {
        ESP_LOGW(TAG, "Thinking timeout -> IDLE");
        s_app.turn_complete_pending = false;
        app_state_set(APP_STATE_IDLE);
        return;
    }

    if (s_app.mic_stop_finalize_pending) {
        s_app.mic_stop_finalize_pending = false;
        s_app.mic_drain_deadline_ms = 0;
        if (!provider_runtime_terminal_stop_mic()) {
            ESP_LOGW(TAG, "terminal activity_end failed");
        }
        app_state_set(APP_STATE_THINKING);
        return;
    }

    if (s_app.mic_drain_pending &&
        s_app.mic_drain_deadline_ms > 0 &&
        now_ms >= s_app.mic_drain_deadline_ms) {
        ESP_LOGW(TAG, "Mic drain deadline hit -> finalize turn");
        s_app.mic_drain_pending = false;
        s_app.mic_drain_deadline_ms = 0;
        app_mic_ring_reset();
        s_app.mic_stop_finalize_pending = true;
        return;
    }

    if (state == APP_STATE_INITIALIZING &&
        s_app.terminal_initialize_deadline_ms > 0 &&
        now_ms >= s_app.terminal_initialize_deadline_ms) {
        ESP_LOGW(TAG, "Initializing timeout -> IDLE");
        s_app.terminal_initialize_deadline_ms = 0;
        s_app.terminal_open_in_flight = false;
        app_state_set(APP_STATE_IDLE);
        return;
    }

    if (state == APP_STATE_LISTENING &&
        (now_ms - s_app.state_since_ms) > 60000LL) {
        ESP_LOGW(TAG, "Listening timeout -> stop mic, keep session");
        s_app.ptt_active = false;
        s_app.mic_drain_pending = false;
        s_app.mic_stop_finalize_pending = false;
        s_app.turn_complete_pending = false;
        s_app.mic_drain_deadline_ms = 0;
        app_stop_mic_capture();
        app_mic_ring_reset();
        if (!provider_runtime_terminal_stop_mic()) {
            ESP_LOGW(TAG, "terminal activity_end failed on listening timeout");
        }
        app_state_set(APP_STATE_THINKING);
        return;
    }

    if (state == APP_STATE_TALKING &&
        s_app.turn_complete_pending &&
        s_app.last_audio_rx_ms > 0 &&
        (audio_platform_speaker_idle() || (now_ms - s_app.last_audio_rx_ms) > 600LL)) {
        ESP_LOGI(TAG, "Talking drain complete -> IDLE");
        s_app.turn_complete_pending = false;
        app_state_set(APP_STATE_IDLE);
    }
}

bool app_bootstrap(const board_profile_t *board)
{
    ui_shell_config_t shell_config;
    uint8_t volume = 70;
    web_server_handle_t server = NULL;
    char saved_ssid[64];
    char saved_pass[64];
    bool connected = false;

    if (board == NULL) {
        return false;
    }

    ESP_LOGI(TAG, "Bootstrapping board id=%s label=%s", board->board_id, board->board_label);

    if (!storage_platform_init()) {
        ESP_LOGE(TAG, "storage_platform_init failed");
        return false;
    }

    app_state_init();

    if (!settings_service_init()) {
        ESP_LOGE(TAG, "settings_service_init failed");
        return false;
    }
    if (!security_auth_init()) {
        ESP_LOGE(TAG, "security_auth_init failed");
        return false;
    }
    if (!wifi_setup_service_init()) {
        ESP_LOGE(TAG, "wifi_setup_service_init failed");
        return false;
    }
    if (!orb_service_init()) {
        ESP_LOGE(TAG, "orb_service_init failed");
        return false;
    }
    if (!ota_service_init()) {
        ESP_LOGE(TAG, "ota_service_init failed");
        return false;
    }
    if (!display_platform_init(board)) {
        ESP_LOGE(TAG, "display_platform_init failed");
        return false;
    }
    if (!input_platform_init(board)) {
        ESP_LOGE(TAG, "input_platform_init failed");
        return false;
    }
    if (!audio_platform_init(board)) {
        ESP_LOGE(TAG, "audio_platform_init failed");
        return false;
    }
    if (!power_platform_init(board)) {
        ESP_LOGE(TAG, "power_platform_init failed");
        return false;
    }

    settings_service_get_volume(&volume);
    audio_platform_set_volume(volume);

    memset(&shell_config, 0, sizeof(shell_config));
    shell_config.on_primary_tap_when_idle = app_on_idle_tap;
    shell_config.on_primary_tap_when_listening = app_on_listening_tap;
    shell_config.on_error_tap = app_on_error_tap;
    shell_config.on_notification_selected = app_on_notification_selected;
    shell_config.on_volume_changed = app_on_volume_changed;

    if (!ui_shell_init(board, &shell_config)) {
        ESP_LOGE(TAG, "ui_shell_init failed");
        return false;
    }

    if (!network_platform_init(board)) {
        ESP_LOGE(TAG, "network_platform_init failed");
        return false;
    }
    memset(saved_ssid, 0, sizeof(saved_ssid));
    memset(saved_pass, 0, sizeof(saved_pass));
    if (wifi_setup_service_get_saved_ssid(saved_ssid, sizeof(saved_ssid))) {
        wifi_setup_service_get_saved_password(saved_pass, sizeof(saved_pass));
        connected = network_platform_connect_sta(saved_ssid, saved_pass);
        ESP_LOGI(TAG, "Saved Wi-Fi connect %s for ssid=%s", connected ? "succeeded" : "failed", saved_ssid);
        ESP_LOGI(TAG, "HEAP after wifi: internal_free=%u spiram_free=%u largest_internal=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        if (connected) {
            network_platform_stop_setup_ap();
        }
    } else {
        if (!network_platform_start_setup_ap()) {
            ESP_LOGE(TAG, "setup AP start failed");
            return false;
        }
        if (!wifi_provisioning_start()) {
            ESP_LOGE(TAG, "wifi_provisioning_start failed");
            return false;
        }
        input_platform_register_callback(app_on_input_event, NULL);
        app_state_register_callback(app_on_state_changed, NULL);
        ui_shell_sync_from_services();
        app_state_set(APP_STATE_IDLE);
        s_app.board = board;
        s_app.bootstrapped = true;
        s_app.provisioning_only = true;
        ESP_LOGI(TAG, "Provisioning mode active for board id=%s", board->board_id);
        return true;
    }
    if (!connected) {
        if (!network_platform_start_setup_ap_background()) {
            ESP_LOGW(TAG, "setup AP start failed");
        } else {
            ESP_LOGI(TAG, "setup AP enabled");
        }
    }
    if (!provider_runtime_init()) {
        ESP_LOGE(TAG, "provider_runtime_init failed");
        return false;
    }
    if (!provider_state_bridge_init()) {
        ESP_LOGE(TAG, "provider_state_bridge_init failed");
        return false;
    }
    if (!provider_runtime_terminal_register_listener(app_on_terminal_event, NULL)) {
        ESP_LOGE(TAG, "provider terminal listener registration failed");
        return false;
    }
    if (!web_server_platform_start(&server)) {
        ESP_LOGE(TAG, "web_server_platform_start failed");
        return false;
    }
    if (!web_shell_register_routes(server)) {
        ESP_LOGE(TAG, "web_shell_register_routes failed");
        return false;
    }
    if (!provider_web_bridge_register_routes(server)) {
        ESP_LOGE(TAG, "provider_web_bridge_register_routes failed");
        return false;
    }

    input_platform_register_callback(app_on_input_event, NULL);
    app_state_register_callback(app_on_state_changed, NULL);
    ui_shell_sync_from_services();
    provider_state_bridge_sync();
    s_app.board = board;
    s_app.bootstrapped = true;
    if (s_app.terminal_open_task == NULL) {
        if (xTaskCreatePinnedToCore(app_terminal_open_task,
                                    "app_term_open",
                                    12288,
                                    NULL,
                                    5,
                                    &s_app.terminal_open_task,
                                    1) != pdPASS) {
            ESP_LOGE(TAG, "app_term_open task create failed");
            return false;
        }
    }
    if (s_app.mic_stream_task == NULL) {
        xTaskCreatePinnedToCore(app_mic_stream_task, "app_mic_stream", 12288, NULL, 5, &s_app.mic_stream_task, 1);
    }
    ESP_LOGI(TAG, "Bootstrap complete for board id=%s", board->board_id);
    ESP_LOGI(TAG, "HEAP at boot complete: internal_free=%u spiram_free=%u largest_internal=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return true;
}

bool app_bootstrap_by_id(const char *board_id)
{
    const board_profile_t *board = board_registry_find(board_id);

    if (board == NULL) {
        return false;
    }

    return app_bootstrap(board);
}

const board_profile_t *app_get_active_board(void)
{
    return s_app.board;
}

void app_run(void)
{
    if (!s_app.bootstrapped) {
        if (!app_bootstrap(board_registry_get_default())) {
            return;
        }
    }

    if (s_app.provisioning_only) {
        while (true) {
            input_platform_process();
            ui_shell_process();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    while (true) {
        input_platform_process();
        app_process_turn_state();
        provider_runtime_process();
        app_process_replay_send();
        app_process_replay_request();
        ui_shell_process();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
