#include "anim_registry.h"

#include <math.h>

#define ORB_TWO_PI 6.28318530f

static void draw_line_seg(lv_draw_ctx_t *dc, int x1, int y1, int x2, int y2, int w, lv_color_t c, lv_opa_t opa)
{
    lv_draw_line_dsc_t d;
    lv_point_t p1;
    lv_point_t p2;

    lv_draw_line_dsc_init(&d);
    d.color = c;
    d.width = (lv_coord_t)w;
    d.opa = opa;
    d.round_start = 1;
    d.round_end = 1;
    p1.x = (lv_coord_t)x1;
    p1.y = (lv_coord_t)y1;
    p2.x = (lv_coord_t)x2;
    p2.y = (lv_coord_t)y2;
    lv_draw_line(dc, &d, &p1, &p2);
}

static void lissajous_draw(lv_obj_t *screen, const anim_state_cfg_t *cfg, const anim_ctx_t *ctx)
{
    lv_draw_ctx_t *dc = (lv_draw_ctx_t *)ctx->draw_ctx;
    int cx = DESKBOT_DISPLAY_CX;
    int cy = DESKBOT_DISPLAY_CY;
    int r = DESKBOT_DISPLAY_R;
    float t = ctx->t * cfg->speed * 30.0f;
    lv_draw_rect_dsc_t bg;
    lv_area_t ba;
    float f_x;
    float f_y;
    float delta;
    int segments;
    int steps = 120;
    float amp;
    lv_point_t pts[121];

    (void)screen;

    lv_draw_rect_dsc_init(&bg);
    bg.bg_color = lv_color_hex(0x000000);
    bg.bg_opa = LV_OPA_COVER;
    bg.radius = LV_RADIUS_CIRCLE;
    bg.border_width = 0;
    bg.shadow_width = 0;
    ba.x1 = cx - r;
    ba.y1 = cy - r;
    ba.x2 = cx + r;
    ba.y2 = cy + r;
    lv_draw_rect(dc, &bg, &ba);

    f_x = cfg->params[0];
    f_y = cfg->params[1] + cfg->params[2] * sinf(t * 0.12f);
    delta = t * 0.18f;
    segments = (int)cfg->params[3];
    if (segments < 1) {
        segments = 14;
    }
    amp = r * 0.82f;

    for (int i = 0; i <= steps; ++i) {
        float u = (float)i / steps * ORB_TWO_PI;
        pts[i].x = (lv_coord_t)(cx + (int)(amp * 0.5f * sinf(f_x * u + delta)));
        pts[i].y = (lv_coord_t)(cy + (int)(amp * 0.5f * sinf(f_y * u)));
    }

    for (int s = 0; s < segments; ++s) {
        float h = (float)s / segments * ORB_TWO_PI + ctx->t * 0.3f;
        lv_color_t col = lv_color_make((uint8_t)(80 + 80 * sinf(h)), (uint8_t)(160 + 80 * sinf(h + 2.1f)), (uint8_t)(220 + 35 * sinf(h + 4.2f)));
        int start = s * steps / segments;
        int end = (s + 1) * steps / segments;

        for (int i = start; i < end; ++i) {
            draw_line_seg(dc, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, (int)(6 * r / 180.0f), col, (lv_opa_t)25);
            draw_line_seg(dc, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, (int)(2 * r / 180.0f), col, (lv_opa_t)200);
        }
    }
}

static const anim_state_cfg_t lissajous_states[ANIM_STATE_COUNT] = {
    [ANIM_STATE_IDLE] = {.speed = 0.18f, .primary = DESKBOT_COLOR(0, 196, 180), .secondary = DESKBOT_COLOR(74, 111, 255), .tertiary = DESKBOT_COLOR(155, 89, 255), .params = {3.0f, 2.0f, 0.06f, 14.0f}},
    [ANIM_STATE_LISTENING] = {.speed = 0.55f, .primary = DESKBOT_COLOR(0, 240, 255), .secondary = DESKBOT_COLOR(0, 102, 255), .tertiary = DESKBOT_COLOR(0, 170, 255), .params = {3.0f, 2.0f, 0.12f, 14.0f}},
    [ANIM_STATE_THINKING] = {.speed = 1.20f, .primary = DESKBOT_COLOR(102, 51, 204), .secondary = DESKBOT_COLOR(170, 119, 255), .tertiary = DESKBOT_COLOR(204, 170, 255), .params = {4.0f, 3.0f, 0.22f, 14.0f}},
    [ANIM_STATE_RESPONDING] = {.speed = 0.30f, .primary = DESKBOT_COLOR(0, 255, 212), .secondary = DESKBOT_COLOR(0, 196, 180), .tertiary = DESKBOT_COLOR(74, 111, 255), .params = {3.0f, 2.0f, 0.04f, 14.0f}},
};

const anim_descriptor_t g_anim_lissajous = {
    .id = ANIM_LISSAJOUS,
    .name = "LISSAJOUS",
    .description = "Chromatic Lissajous figure with drifting frequency.",
    .draw_fn = lissajous_draw,
    .states = lissajous_states
};
