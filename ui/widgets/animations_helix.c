#include "anim_registry.h"

#include <math.h>

#define ORB_PI 3.14159265f

static void helix_fill_circle(lv_draw_ctx_t *dc, int cx, int cy, int r, lv_color_t c, lv_opa_t opa)
{
    lv_draw_rect_dsc_t d;
    lv_area_t a;

    if (r <= 0) {
        return;
    }

    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = opa;
    d.radius = LV_RADIUS_CIRCLE;
    d.border_width = 0;
    d.shadow_width = 0;
    a.x1 = cx - r;
    a.y1 = cy - r;
    a.x2 = cx + r;
    a.y2 = cy + r;
    lv_draw_rect(dc, &d, &a);
}

static void helix_draw(lv_obj_t *screen, const anim_state_cfg_t *cfg, const anim_ctx_t *ctx)
{
    lv_draw_ctx_t *dc = (lv_draw_ctx_t *)ctx->draw_ctx;
    int cx = DESKBOT_DISPLAY_CX;
    int cy = DESKBOT_DISPLAY_CY;
    int r = DESKBOT_DISPLAY_R;
    float t = ctx->t * cfg->speed * 30.0f;
    lv_color_t colors[2] = {
        DESKBOT_TO_LV_COLOR(cfg->primary),
        DESKBOT_TO_LV_COLOR(cfg->secondary),
    };
    int n = 24;
    float p = t * 2.0f;

    (void)screen;

    helix_fill_circle(dc, cx, cy, r, lv_color_hex(0x000000), LV_OPA_COVER);

    for (int i = 0; i < n; ++i) {
        float f = (float)i / n;
        float y = -r * 0.8f + f * r * 1.6f;
        float rr = r * 0.35f * sinf(f * ORB_PI);

        for (int side = 0; side < 2; ++side) {
            float off = side * ORB_PI + f * ORB_PI * 2.5f + p;
            int px = cx + (int)(cosf(off) * rr);
            int py = cy + (int)y;
            float z = sinf(off);
            int sz = (int)(r * 0.04f * (1.2f + z * 0.5f));

            helix_fill_circle(dc, px, py, sz * 2, colors[side], (lv_opa_t)((0.5f + z * 0.4f) * 60.0f));
            helix_fill_circle(dc, px, py, sz, colors[side], (lv_opa_t)((0.6f + z * 0.4f) * 255.0f));
        }
    }
}

static const anim_state_cfg_t helix_states[ANIM_STATE_COUNT] = {
    [ANIM_STATE_IDLE] = {.speed = 0.40f, .primary = DESKBOT_COLOR(0, 196, 180), .secondary = DESKBOT_COLOR(74, 111, 255)},
    [ANIM_STATE_LISTENING] = {.speed = 1.00f, .primary = DESKBOT_COLOR(0, 240, 255), .secondary = DESKBOT_COLOR(0, 102, 255)},
    [ANIM_STATE_THINKING] = {.speed = 2.20f, .primary = DESKBOT_COLOR(170, 119, 255), .secondary = DESKBOT_COLOR(102, 51, 204)},
    [ANIM_STATE_RESPONDING] = {.speed = 0.65f, .primary = DESKBOT_COLOR(0, 255, 212), .secondary = DESKBOT_COLOR(0, 196, 180)},
};

const anim_descriptor_t g_anim_helix = {
    .id = ANIM_HELIX,
    .name = "HELIX",
    .description = "Double helix DNA strand rotating in 3D perspective.",
    .draw_fn = helix_draw,
    .states = helix_states
};
