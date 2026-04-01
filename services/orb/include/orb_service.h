#ifndef ORB_SERVICE_H
#define ORB_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ORB_STATE_COUNT 8
#define ORB_PARAM_COUNT 12

typedef enum {
    ORB_STATE_IDLE = 0,
    ORB_STATE_CONNECTING,
    ORB_STATE_LISTENING,
    ORB_STATE_THINKING,
    ORB_STATE_TALKING,
    ORB_STATE_ERROR,
    ORB_STATE_SLEEP,
    ORB_STATE_LOCKED,
} orb_state_id_t;

typedef struct {
    float speed;
    uint32_t primary;
    uint32_t secondary;
    uint32_t tertiary;
    float intensity;
    float params[ORB_PARAM_COUNT];
} orb_state_config_t;

typedef struct {
    uint8_t theme_id;
    float global_speed;
    orb_state_config_t states[ORB_STATE_COUNT];
} orb_config_t;

bool orb_service_init(void);
bool orb_service_get_config(orb_config_t *out_config);
bool orb_service_set_config(const orb_config_t *config);

#endif
