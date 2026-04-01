#include "orb_widget.h"

#include <stdlib.h>
#include <string.h>

#include "anim_registry.h"
#include "esp_timer.h"

#define ORB_FPS_MS 16U

typedef struct {
    lv_obj_t *obj;
    lv_timer_t *timer;
    presentation_visual_t visual;
    orb_config_t config;
    bool has_config;
    float level;
    float state_time_s;
    float last_tick_s;
    uint32_t frame;
    bool transitioning;
    uint8_t active_anim_id;
} orb_widget_runtime_t;

static orb_widget_snapshot_t s_snapshot;
static orb_widget_runtime_t s_runtime;

static float clamp01(float value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static const anim_descriptor_t *active_animation(void)
{
    const anim_descriptor_t *anim = NULL;

    if (s_runtime.has_config) {
        anim = anim_registry_find_by_id(s_runtime.config.theme_id);
    }
    if (anim == NULL && anim_registry_count() > 0) {
        anim = anim_registry_get(0);
    }
    return anim;
}

static void sync_animation_lifecycle(const anim_descriptor_t *next_anim)
{
    const anim_descriptor_t *current_anim = NULL;

    if (s_runtime.active_anim_id != ANIM_ID_MAX) {
        current_anim = anim_registry_find_by_id(s_runtime.active_anim_id);
    }

    if (current_anim != NULL && (next_anim == NULL || current_anim->id != next_anim->id)) {
        if (current_anim->deinit_fn != NULL) {
            current_anim->deinit_fn();
        }
        s_runtime.active_anim_id = ANIM_ID_MAX;
    }

    if (next_anim != NULL && s_runtime.active_anim_id != next_anim->id) {
        if (next_anim->init_fn != NULL) {
            next_anim->init_fn();
        }
        s_runtime.active_anim_id = next_anim->id;
    }
}

static orb_state_id_t visual_to_orb_state(presentation_visual_t visual)
{
    switch (visual) {
    case PRESENTATION_VISUAL_CONNECTING:
        return ORB_STATE_CONNECTING;
    case PRESENTATION_VISUAL_LISTENING:
        return ORB_STATE_LISTENING;
    case PRESENTATION_VISUAL_THINKING:
        return ORB_STATE_THINKING;
    case PRESENTATION_VISUAL_TALKING:
        return ORB_STATE_TALKING;
    case PRESENTATION_VISUAL_ERROR:
        return ORB_STATE_ERROR;
    case PRESENTATION_VISUAL_SLEEP:
        return ORB_STATE_SLEEP;
    case PRESENTATION_VISUAL_LOCKED:
        return ORB_STATE_LOCKED;
    case PRESENTATION_VISUAL_IDLE:
    default:
        return ORB_STATE_IDLE;
    }
}

static const orb_state_config_t *current_state_config(void)
{
    orb_state_id_t state_id = visual_to_orb_state(s_runtime.visual);

    if (!s_runtime.has_config) {
        return NULL;
    }

    if ((int)state_id < 0 || state_id >= ORB_STATE_COUNT) {
        return NULL;
    }

    return &s_runtime.config.states[state_id];
}

static void build_anim_cfg(const orb_state_config_t *src, anim_state_cfg_t *dst)
{
    memset(dst, 0, sizeof(*dst));
    if (src == NULL || dst == NULL) {
        return;
    }

    dst->speed = src->speed;
    dst->primary = src->primary;
    dst->secondary = src->secondary;
    dst->tertiary = src->tertiary;
    dst->intensity = src->intensity;
    memcpy(dst->params, src->params, sizeof(dst->params));
}

static void orb_delete_cb(lv_event_t *event)
{
    lv_obj_t *obj = lv_event_get_target(event);

    if (obj == s_runtime.obj) {
        sync_animation_lifecycle(NULL);
        if (s_runtime.timer != NULL) {
            lv_timer_del(s_runtime.timer);
            s_runtime.timer = NULL;
        }
        s_runtime.obj = NULL;
    }
}

static void orb_draw_event_cb(lv_event_t *event)
{
    const anim_descriptor_t *anim;
    const orb_state_config_t *state_cfg;
    anim_state_cfg_t anim_cfg;
    anim_ctx_t ctx;

    if (lv_event_get_code(event) != LV_EVENT_DRAW_MAIN) {
        return;
    }

    anim = active_animation();
    state_cfg = current_state_config();
    if (anim == NULL || anim->draw_fn == NULL || state_cfg == NULL) {
        return;
    }

    build_anim_cfg(state_cfg, &anim_cfg);
    ctx.t = (float)(esp_timer_get_time() / 1000000.0) * (s_runtime.has_config ? s_runtime.config.global_speed : 1.0f);
    ctx.dt = (float)ORB_FPS_MS / 1000.0f;
    ctx.state_t = s_runtime.state_time_s;
    ctx.audio_level = s_runtime.level;
    ctx.transitioning = s_runtime.transitioning;
    ctx.frame = s_runtime.frame;
    ctx.anim_id = anim->id;
    ctx.draw_ctx = lv_event_get_draw_ctx(event);

    anim->draw_fn(lv_event_get_target(event), &anim_cfg, &ctx);
}

static void orb_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    s_runtime.level *= 0.985f;
    s_runtime.state_time_s += (float)ORB_FPS_MS / 1000.0f;
    if (s_runtime.transitioning) {
        s_runtime.last_tick_s += (float)ORB_FPS_MS / 1000.0f;
        if (s_runtime.last_tick_s >= 0.30f) {
            s_runtime.transitioning = false;
            s_runtime.last_tick_s = 0.0f;
        }
    }
    s_runtime.frame++;

    if (s_runtime.obj != NULL) {
        lv_obj_invalidate(s_runtime.obj);
    }
}

size_t orb_widget_theme_count(void)
{
    return (size_t)anim_registry_count();
}

const char *orb_widget_theme_name(uint8_t theme_id)
{
    const anim_descriptor_t *anim = anim_registry_find_by_id(theme_id);

    return anim != NULL ? anim->name : NULL;
}

bool orb_widget_theme_apply_defaults(uint8_t theme_id, orb_config_t *config)
{
    const anim_descriptor_t *anim = anim_registry_find_by_id(theme_id);

    if (config == NULL) {
        return false;
    }
    if (anim == NULL) {
        anim = anim_registry_get(0);
    }
    if (anim == NULL || anim->states == NULL) {
        return false;
    }

    config->theme_id = anim->id;
    config->global_speed = 1.0f;

    for (int i = 0; i < ORB_STATE_COUNT; ++i) {
        const anim_state_cfg_t *src = NULL;
        orb_state_config_t *dst = &config->states[i];

        switch ((orb_state_id_t)i) {
        case ORB_STATE_IDLE:
        case ORB_STATE_SLEEP:
        case ORB_STATE_LOCKED:
            src = &anim->states[ANIM_STATE_IDLE];
            break;
        case ORB_STATE_CONNECTING:
        case ORB_STATE_THINKING:
        case ORB_STATE_ERROR:
            src = &anim->states[ANIM_STATE_THINKING];
            break;
        case ORB_STATE_LISTENING:
            src = &anim->states[ANIM_STATE_LISTENING];
            break;
        case ORB_STATE_TALKING:
            src = &anim->states[ANIM_STATE_RESPONDING];
            break;
        default:
            src = &anim->states[ANIM_STATE_IDLE];
            break;
        }

        dst->speed = src->speed;
        dst->primary = src->primary;
        dst->secondary = src->secondary;
        dst->tertiary = src->tertiary;
        dst->intensity = src->intensity;
        memcpy(dst->params, src->params, sizeof(dst->params));
    }

    return true;
}

bool orb_widget_init(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    memset(&s_runtime, 0, sizeof(s_runtime));
    s_runtime.active_anim_id = ANIM_ID_MAX;
    s_snapshot.initialized = true;
    s_snapshot.visual = PRESENTATION_VISUAL_IDLE;
    s_runtime.visual = PRESENTATION_VISUAL_IDLE;
    return true;
}

bool orb_widget_bind_lvgl(lv_obj_t *parent, int16_t x, int16_t y, uint16_t width, uint16_t height)
{
    if (parent == NULL) {
        return false;
    }

    if (s_runtime.obj != NULL) {
        lv_obj_del(s_runtime.obj);
        s_runtime.obj = NULL;
    }
    if (s_runtime.timer != NULL) {
        lv_timer_del(s_runtime.timer);
        s_runtime.timer = NULL;
    }

    s_runtime.obj = lv_obj_create(parent);
    if (s_runtime.obj == NULL) {
        return false;
    }

    lv_obj_clear_flag(s_runtime.obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_runtime.obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_runtime.obj, 0, 0);
    lv_obj_set_style_outline_width(s_runtime.obj, 0, 0);
    lv_obj_set_style_shadow_width(s_runtime.obj, 0, 0);
    lv_obj_set_style_radius(s_runtime.obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(s_runtime.obj, 0, 0);
    lv_obj_set_pos(s_runtime.obj, x, y);
    lv_obj_set_size(s_runtime.obj, width, height);
    lv_obj_add_event_cb(s_runtime.obj, orb_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(s_runtime.obj, orb_delete_cb, LV_EVENT_DELETE, NULL);
    s_runtime.timer = lv_timer_create(orb_timer_cb, ORB_FPS_MS, NULL);
    sync_animation_lifecycle(active_animation());
    lv_obj_invalidate(s_runtime.obj);
    return true;
}

void orb_widget_apply_presentation(presentation_visual_t visual)
{
    if (s_runtime.visual != visual) {
        s_runtime.visual = visual;
        s_runtime.state_time_s = 0.0f;
        s_runtime.last_tick_s = 0.0f;
        s_runtime.transitioning = true;
    }
    s_snapshot.visual = visual;

    if (s_runtime.obj != NULL) {
        lv_obj_invalidate(s_runtime.obj);
    }
}

void orb_widget_apply_config(const orb_config_t *config)
{
    if (config == NULL) {
        sync_animation_lifecycle(NULL);
        s_snapshot.has_config = false;
        s_runtime.has_config = false;
        memset(&s_snapshot.config, 0, sizeof(s_snapshot.config));
        memset(&s_runtime.config, 0, sizeof(s_runtime.config));
        return;
    }

    s_snapshot.config = *config;
    s_snapshot.has_config = true;
    s_runtime.config = *config;
    s_runtime.has_config = true;
    sync_animation_lifecycle(active_animation());

    if (s_runtime.obj != NULL) {
        lv_obj_invalidate(s_runtime.obj);
    }
}

void orb_widget_set_level(float level_0_to_1)
{
    float level = clamp01(level_0_to_1);

    s_runtime.level = s_runtime.level + (level - s_runtime.level) * 0.25f;
}

bool orb_widget_get_snapshot(orb_widget_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return false;
    }

    *out_snapshot = s_snapshot;
    return true;
}
