#include "provider_ui_bridge.h"

#include <string.h>

static void copy_text(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0) {
        return;
    }

    dst[0] = '\0';
    if (src == NULL || src[0] == '\0') {
        return;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"
    strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
#pragma GCC diagnostic pop
}

static void map_status_line(const runtime_snapshot_t *runtime,
                            const provider_terminal_snapshot_t *terminal,
                            provider_ui_view_t *view)
{
    if (runtime == NULL || terminal == NULL || view == NULL) {
        return;
    }

    if (terminal->last_error[0] != '\0') {
        copy_text(view->status_line, sizeof(view->status_line), "Provider Error");
        return;
    }

    switch (runtime->status) {
    case RUNTIME_STATUS_DISCONNECTED:
        copy_text(view->status_line, sizeof(view->status_line), "Provider Disconnected");
        break;
    case RUNTIME_STATUS_CONNECTING:
        copy_text(view->status_line, sizeof(view->status_line), "Provider Connecting");
        break;
    case RUNTIME_STATUS_PAIRING_REQUIRED:
        copy_text(view->status_line, sizeof(view->status_line), "Pairing Required");
        break;
    case RUNTIME_STATUS_PAIR_CODE_READY:
        copy_text(view->status_line, sizeof(view->status_line), "Pair Code Ready");
        break;
    case RUNTIME_STATUS_WAITING_FOR_APPROVAL:
        copy_text(view->status_line, sizeof(view->status_line), "Waiting For Approval");
        break;
    case RUNTIME_STATUS_READY:
        if (terminal->turn_in_progress) {
            copy_text(view->status_line, sizeof(view->status_line), "Provider Turn Active");
        } else if (terminal->mic_active) {
            copy_text(view->status_line, sizeof(view->status_line), "Provider Mic Active");
        } else {
            copy_text(view->status_line, sizeof(view->status_line), "Provider Ready");
        }
        break;
    case RUNTIME_STATUS_ERROR:
    default:
        copy_text(view->status_line, sizeof(view->status_line), "Provider Error");
        break;
    }
}

bool provider_ui_bridge_get_view(provider_ui_view_t *out_view)
{
    runtime_snapshot_t runtime;
    provider_terminal_snapshot_t terminal;

    if (out_view == NULL) {
        return false;
    }

    memset(out_view, 0, sizeof(*out_view));
    memset(&runtime, 0, sizeof(runtime));
    memset(&terminal, 0, sizeof(terminal));

    out_view->available = runtime_control_is_available();
    if (!out_view->available) {
        copy_text(out_view->status_line, sizeof(out_view->status_line), "Provider Unavailable");
        return true;
    }

    if (!runtime_control_get_snapshot(&runtime)) {
        copy_text(out_view->status_line, sizeof(out_view->status_line), "Provider Snapshot Error");
        return false;
    }

    if (!provider_terminal_get_snapshot(&terminal)) {
        memset(&terminal, 0, sizeof(terminal));
    }

    out_view->runtime_status = runtime.status;
    out_view->ready = runtime.status == RUNTIME_STATUS_READY;
    out_view->mic_active = terminal.mic_active;
    out_view->turn_in_progress = terminal.turn_in_progress;
    out_view->session_open = terminal.session_open;
    copy_text(out_view->status_detail, sizeof(out_view->status_detail), runtime.status_detail);
    copy_text(out_view->pair_code, sizeof(out_view->pair_code), runtime.pair_code);
    copy_text(out_view->terminal_text, sizeof(out_view->terminal_text), terminal.last_text);
    copy_text(out_view->terminal_error, sizeof(out_view->terminal_error), terminal.last_error);
    map_status_line(&runtime, &terminal, out_view);
    return true;
}
