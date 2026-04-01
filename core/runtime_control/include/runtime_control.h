#ifndef RUNTIME_CONTROL_H
#define RUNTIME_CONTROL_H

#include <stdbool.h>

typedef enum {
    RUNTIME_STATUS_DISCONNECTED = 0,
    RUNTIME_STATUS_CONNECTING,
    RUNTIME_STATUS_PAIRING_REQUIRED,
    RUNTIME_STATUS_PAIR_CODE_READY,
    RUNTIME_STATUS_WAITING_FOR_APPROVAL,
    RUNTIME_STATUS_READY,
    RUNTIME_STATUS_ERROR,
} runtime_status_t;

typedef struct {
    runtime_status_t status;
    char pair_code[64];
    char status_detail[64];
    bool has_gateway_token;
    bool has_device_token;
} runtime_snapshot_t;

typedef struct {
    bool (*is_available)(void);
    bool (*get_snapshot)(runtime_snapshot_t *out_snapshot);
    bool (*reconnect)(void);
    bool (*full_reset)(void);
} runtime_control_ops_t;

bool runtime_control_register_ops(const runtime_control_ops_t *ops);
bool runtime_control_is_available(void);
bool runtime_control_get_snapshot(runtime_snapshot_t *out_snapshot);
bool runtime_control_reconnect(void);
bool runtime_control_full_reset(void);

#endif
