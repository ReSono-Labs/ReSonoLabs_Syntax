#include "anim_registry.h"

#include <math.h>

#define ORB_PI 3.14159265f

static void compass_fill_circle(lv_draw_ctx_t *dc, int cx, int cy, int r, lv_color_t c, lv_opa_t opa)
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

static void compass_draw_line(lv_draw_ctx_t *dc, int x1, int y1, int x2, int y2, int w, lv_color_t c, lv_opa_t opa)
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

static void compass_draw(lv_obj_t *screen, const anim_state_cfg_t *cfg, const anim_ctx_t *ctx)
{
    lv_draw_ctx_t *dc = (lv_draw_ctx_t *)ctx->draw_ctx;
    int cx = DESKBOT_DISPLAY_CX;
    int cy = DESKBOT_DISPLAY_CY;
    int r = DESKBOT_DISPLAY_R;
    float t = ctx->t * cfg->speed * 30.0f;
    lv_color_t palette[3] = {
        DESKBOT_TO_LV_COLOR(cfg->primary),
        DESKBOT_TO_LV_COLOR(cfg->secondary),
        DESKBOT_TO_LV_COLOR(cfg->tertiary),
    };
    struct {
        float mul;
        float len;
        float w;
        float off;
    } spokes[5] = {
        {0.20f, 0.85f, 2.0f, 0.0f},
        {-0.34f, 0.72f, 1.5f, ORB_PI * 0.5f},
        {0.55f, 0.58f, 1.2f, ORB_PI},
        {-0.78f, 0.45f, 1.0f, ORB_PI * 1.5f},
        {1.10f, 0.35f, 0.8f, ORB_PI * 0.3f},
    };
    lv_point_t tips[5];

    (void)screen;

    compass_fill_circle(dc, cx, cy, r, lv_color_hex(0x000000), LV_OPA_COVER);

    for (int i = 0; i < 5; ++i) {
        float ang = t * spokes[i].mul + spokes[i].off;
        int bx;
        int by;
        lv_color_t col = palette[i % 3];

        tips[i].x = (lv_coord_t)(cx + (int)(cosf(ang) * r * spokes[i].len));
        tips[i].y = (lv_coord_t)(cy + (int)(sinf(ang) * r * spokes[i].len));
        bx = cx - (int)(cosf(ang) * r * spokes[i].len * 0.35f);
        by = cy - (int)(sinf(ang) * r * spokes[i].len * 0.35f);
        compass_draw_line(dc, bx, by, tips[i].x, tips[i].y, (int)(spokes[i].w * 5), col, (lv_opa_t)30);
        compass_draw_line(dc, bx, by, tips[i].x, tips[i].y, (int)spokes[i].w, col, (lv_opa_t)200);
        compass_fill_circle(dc, tips[i].x, tips[i].y, (int)(spokes[i].w * 2.5f), lv_color_hex(0xFFFFFF), (lv_opa_t)215);
    }

    for (int a = 0; a < 5; ++a) {
        for (int b = a + 1; b < 5; ++b) {
            float d = sqrtf((float)((tips[a].x - tips[b].x) * (tips[a].x - tips[b].x) + (tips[a].y - tips[b].y) * (tips[a].y - tips[b].y)));
            float thr = r * 0.28f;

            if (d < thr) {
                float s = 1.0f - d / thr;
                compass_fill_circle(dc, (tips[a].x + tips[b].x) / 2, (tips[a].y + tips[b].y) / 2, (int)(r * 0.08f * s), lv_color_hex(0xFFFFFF), (lv_opa_t)(s * 128.0f));
            }
        }
    }
    compass_fill_circle(dc, cx, cy, 7, lv_color_hex(0xC8F0FF), (lv_opa_t)230);
}

static const anim_state_cfg_t compass_states[ANIM_STATE_COUNT] = {
    [ANIM_STATE_IDLE] = {.speed = 0.25f, .primary = DESKBOT_COLOR(0, 196, 180), .secondary = DESKBOT_COLOR(74, 111, 255), .tertiary = DESKBOT_COLOR(0, 170, 255)},
    [ANIM_STATE_LISTENING] = {.speed = 0.80f, .primary = DESKBOT_COLOR(0, 240, 255), .secondary = DESKBOT_COLOR(0, 102, 255), .tertiary = DESKBOT_COLOR(0, 170, 255)},
    [ANIM_STATE_THINKING] = {.speed = 1.80f, .primary = DESKBOT_COLOR(170, 119, 255), .secondary = DESKBOT_COLOR(102, 51, 204), .tertiary = DESKBOT_COLOR(204, 170, 255)},
    [ANIM_STATE_RESPONDING] = {.speed = 0.50f, .primary = DESKBOT_COLOR(0, 255, 212), .secondary = DESKBOT_COLOR(0, 196, 180), .tertiary = DESKBOT_COLOR(0, 240, 255)},
};

const anim_descriptor_t g_anim_compass = {
    .id = ANIM_COMPASS,
    .name = "COMPASS",
    .description = "Five rotating compass needles with spark connections.",
    .draw_fn = compass_draw,
    .states = compass_states
};
