#ifndef ANIM_REGISTRY_H
#define ANIM_REGISTRY_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DESKBOT_DISPLAY_W 360
#define DESKBOT_DISPLAY_H 360
#define DESKBOT_DISPLAY_CX 180
#define DESKBOT_DISPLAY_CY 180
#define DESKBOT_DISPLAY_R 180

typedef enum {
    ANIM_STATE_IDLE = 0,
    ANIM_STATE_LISTENING = 1,
    ANIM_STATE_THINKING = 2,
    ANIM_STATE_RESPONDING = 3,
    ANIM_STATE_COUNT = 4,
} anim_state_idx_t;

typedef uint32_t deskbot_color_t;

#define DESKBOT_COLOR(r, g, b) ((uint32_t)(((r) << 16) | ((g) << 8) | (b)))
#define DESKBOT_COLOR_R(c) (((c) >> 16) & 0xFF)
#define DESKBOT_COLOR_G(c) (((c) >> 8) & 0xFF)
#define DESKBOT_COLOR_B(c) ((c) & 0xFF)
#define DESKBOT_TO_LV_COLOR(c) lv_color_make(DESKBOT_COLOR_R(c), DESKBOT_COLOR_G(c), DESKBOT_COLOR_B(c))

typedef struct {
    float speed;
    deskbot_color_t primary;
    deskbot_color_t secondary;
    deskbot_color_t tertiary;
    float intensity;
    float params[12];
} anim_state_cfg_t;

typedef struct {
    float t;
    float dt;
    float state_t;
    float audio_level;
    bool transitioning;
    uint32_t frame;
    uint8_t anim_id;
    void *draw_ctx;
} anim_ctx_t;

typedef void (*anim_draw_fn_t)(lv_obj_t *screen, const anim_state_cfg_t *cfg, const anim_ctx_t *ctx);
typedef void (*anim_init_fn_t)(void);
typedef void (*anim_deinit_fn_t)(void);

typedef struct {
    const char *name;
    float min;
    float max;
    float step;
} anim_param_desc_t;

typedef struct {
    uint8_t id;
    const char *name;
    const char *description;
    anim_draw_fn_t draw_fn;
    anim_init_fn_t init_fn;
    anim_deinit_fn_t deinit_fn;
    const anim_state_cfg_t *states;
    const anim_param_desc_t *param_descs;
    uint8_t lvgl_calls;
} anim_descriptor_t;

typedef enum {
    ANIM_NUCLEUS = 0,
    ANIM_VORTEX = 1,
    ANIM_LISSAJOUS = 2,
    ANIM_LATTICE = 3,
    ANIM_COMPASS = 4,
    ANIM_AURORA = 5,
    ANIM_RADAR = 6,
    ANIM_HELIX = 7,
    ANIM_USER_BASE = 16,
    ANIM_ID_MAX = 64,
} anim_id_t;

int anim_registry_count(void);
const anim_descriptor_t *anim_registry_get(int index);

const anim_descriptor_t *anim_registry_find_by_id(uint8_t id);
const anim_descriptor_t *anim_registry_find_by_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif
