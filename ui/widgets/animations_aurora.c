#include "anim_registry.h"

#include <math.h>

#define ORB_TWO_PI 6.28318530f

static void aurora_fill_circle(lv_draw_ctx_t *dc, int cx, int cy, int r, lv_color_t c, lv_opa_t opa)
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

static void aurora_draw_line(lv_draw_ctx_t *dc, int x1, int y1, int x2, int y2, int w, lv_color_t c, lv_opa_t opa)
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

static void aurora_draw(lv_obj_t *screen, const anim_state_cfg_t *cfg, const anim_ctx_t *ctx)
{
    lv_draw_ctx_t *dc = (lv_draw_ctx_t *)ctx->draw_ctx;
    int cx = DESKBOT_DISPLAY_CX;
    int cy = DESKBOT_DISPLAY_CY;
    int r = DESKBOT_DISPLAY_R;
    float t = ctx->t * cfg->speed * 30.0f;
    lv_color_t colors[3] = {
        DESKBOT_TO_LV_COLOR(cfg->primary),
        DESKBOT_TO_LV_COLOR(cfg->secondary),
        DESKBOT_TO_LV_COLOR(cfg->tertiary),
    };
    int steps = 60;

    (void)screen;

    aurora_fill_circle(dc, cx, cy, r, lv_color_hex(0x000000), LV_OPA_COVER);

    for (int i = 0; i < 3; ++i) {
        float off = i * (float)M_PI * 0.4f;
        lv_point_t last = {0, 0};

        for (int s = 0; s <= steps; ++s) {
            float f = (float)s / steps;
            float ang = f * ORB_TWO_PI;
            float rr = r * (0.45f + 0.15f * sinf(ang * 3.0f + t * 1.5f + off) + 0.10f * sinf(ang * 7.0f - t * 0.8f));
            int x = cx + (int)(cosf(ang) * rr);
            int y = cy + (int)(sinf(ang) * rr);

            if (s > 0) {
                aurora_draw_line(dc, last.x, last.y, x, y, (int)(r * 0.065f), colors[i], (lv_opa_t)25);
                aurora_draw_line(dc, last.x, last.y, x, y, (int)(r * 0.012f), colors[i], (lv_opa_t)210);
            }
            last.x = (lv_coord_t)x;
            last.y = (lv_coord_t)y;
        }
    }
}

static const anim_state_cfg_t aurora_states[ANIM_STATE_COUNT] = {
    [ANIM_STATE_IDLE] = {.speed = 0.50f, .primary = DESKBOT_COLOR(0, 196, 180), .secondary = DESKBOT_COLOR(74, 111, 255), .tertiary = DESKBOT_COLOR(59, 59, 138)},
    [ANIM_STATE_LISTENING] = {.speed = 1.50f, .primary = DESKBOT_COLOR(0, 240, 255), .secondary = DESKBOT_COLOR(0, 102, 255), .tertiary = DESKBOT_COLOR(0, 170, 255)},
    [ANIM_STATE_THINKING] = {.speed = 2.50f, .primary = DESKBOT_COLOR(102, 51, 204), .secondary = DESKBOT_COLOR(170, 119, 255), .tertiary = DESKBOT_COLOR(204, 170, 255)},
    [ANIM_STATE_RESPONDING] = {.speed = 0.90f, .primary = DESKBOT_COLOR(0, 255, 212), .secondary = DESKBOT_COLOR(0, 196, 180), .tertiary = DESKBOT_COLOR(74, 111, 255)},
};

const anim_descriptor_t g_anim_aurora = {
    .id = ANIM_AURORA,
    .name = "AURORA",
    .description = "Three overlapping aurora ribbons in polar coordinates.",
    .draw_fn = aurora_draw,
    .states = aurora_states
};
