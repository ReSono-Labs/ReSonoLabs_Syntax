#include "anim_registry.h"

#include <math.h>

#define ORB_TWO_PI 6.28318530f

static void fill_circle(lv_draw_ctx_t *dc, int cx, int cy, int r, lv_color_t c, lv_opa_t opa)
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

static void vortex_draw(lv_obj_t *screen, const anim_state_cfg_t *cfg, const anim_ctx_t *ctx)
{
    lv_draw_ctx_t *dc = (lv_draw_ctx_t *)ctx->draw_ctx;
    int cx = DESKBOT_DISPLAY_CX;
    int cy = DESKBOT_DISPLAY_CY;
    int r = DESKBOT_DISPLAY_R;
    float t = ctx->t * cfg->speed * 30.0f;
    int n = (int)cfg->params[0];
    float ga = ORB_TWO_PI * (1.0f - 1.0f / 1.61803398875f);
    lv_color_t col = DESKBOT_TO_LV_COLOR(cfg->primary);

    (void)screen;

    fill_circle(dc, cx, cy, r, lv_color_hex(0x000000), LV_OPA_COVER);

    if (n < 4) {
        n = 36;
    }

    for (int i = 0; i < n; ++i) {
        float f = (float)i / n;
        float rr = r * 0.88f * sqrtf(f);
        float ang = i * ga + t * (0.6f + (1.0f - f) * 1.2f);
        int px = cx + (int)(cosf(ang) * rr);
        int py = cy + (int)(sinf(ang) * rr);
        float sz = (2.5f + (1.0f - f) * 5.5f) * (1.0f + 0.25f * sinf(ctx->t * 1.8f + i * 0.4f));
        float al = 0.35f + 0.65f * (1.0f - f * 0.7f);

        fill_circle(dc, px, py, (int)(sz * 2.2f * r / 180.0f), col, (lv_opa_t)(al * 0.12f * 255.0f));
        fill_circle(dc, px, py, (int)(sz * r / 180.0f), col, (lv_opa_t)(al * 255.0f));
    }
    fill_circle(dc, cx, cy, 5, lv_color_hex(0xC8FFF0), (lv_opa_t)((0.7f + 0.3f * sinf(ctx->t * 2.5f)) * 255.0f));
}

static const anim_state_cfg_t vortex_states[ANIM_STATE_COUNT] = {
    [ANIM_STATE_IDLE] = {.speed = 0.40f, .primary = DESKBOT_COLOR(0, 196, 180), .params = {36.0f}},
    [ANIM_STATE_LISTENING] = {.speed = 1.20f, .primary = DESKBOT_COLOR(0, 240, 255), .params = {36.0f}},
    [ANIM_STATE_THINKING] = {.speed = 2.20f, .primary = DESKBOT_COLOR(170, 119, 255), .params = {42.0f}},
    [ANIM_STATE_RESPONDING] = {.speed = 0.70f, .primary = DESKBOT_COLOR(0, 255, 212), .params = {36.0f}},
};

const anim_descriptor_t g_anim_vortex = {
    .id = ANIM_VORTEX,
    .name = "VORTEX",
    .description = "Golden-ratio spiral of glowing particles.",
    .draw_fn = vortex_draw,
    .states = vortex_states
};
