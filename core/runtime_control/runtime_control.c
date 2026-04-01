#include "runtime_control.h"

static runtime_control_ops_t s_ops;
static bool s_has_ops;

bool runtime_control_register_ops(const runtime_control_ops_t *ops)
{
    if (ops == 0) {
        s_has_ops = false;
        s_ops.is_available = 0;
        s_ops.get_snapshot = 0;
        s_ops.reconnect = 0;
        s_ops.full_reset = 0;
        return true;
    }

    s_ops = *ops;
    s_has_ops = true;
    return true;
}

bool runtime_control_is_available(void)
{
    if (s_has_ops && s_ops.is_available != 0) {
        return s_ops.is_available();
    }
    return false;
}

bool runtime_control_get_snapshot(runtime_snapshot_t *out_snapshot)
{
    if (out_snapshot == 0) {
        return false;
    }

    if (s_has_ops && s_ops.get_snapshot != 0) {
        return s_ops.get_snapshot(out_snapshot);
    }

    out_snapshot->status = RUNTIME_STATUS_DISCONNECTED;
    out_snapshot->pair_code[0] = '\0';
    out_snapshot->status_detail[0] = '\0';
    out_snapshot->has_gateway_token = false;
    out_snapshot->has_device_token = false;
    return true;
}

bool runtime_control_reconnect(void)
{
    if (s_has_ops && s_ops.reconnect != 0) {
        return s_ops.reconnect();
    }
    return false;
}

bool runtime_control_full_reset(void)
{
    if (s_has_ops && s_ops.full_reset != 0) {
        return s_ops.full_reset();
    }
    return false;
}
