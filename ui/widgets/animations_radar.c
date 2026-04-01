#include "anim_registry.h"

#include <math.h>

#define ORB_PI 3.14159265f
#define ORB_TWO_PI 6.28318530f

static void radar_fill_circle(lv_draw_ctx_t *dc, int cx, int cy, int r, lv_color_t c, lv_opa_t opa)
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

static void radar_draw_line(lv_draw_ctx_t *dc, int x1, int y1, int x2, int y2, int w, lv_color_t c, lv_opa_t opa)
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

static void radar_draw_arc(lv_draw_ctx_t *dc, int cx, int cy, int r, int w, uint16_t s, uint16_t e, lv_color_t c, lv_opa_t opa)
{
    lv_draw_arc_dsc_t d;
    lv_point_t ctr;

    if (r <= 0 || w <= 0) {
        return;
    }

    lv_draw_arc_dsc_init(&d);
    d.color = c;
    d.opa = opa;
    d.width = (uint16_t)w;
    d.rounded = 1;
    ctr.x = (lv_coord_t)cx;
    ctr.y = (lv_coord_t)cy;
    lv_draw_arc(dc, &d, &ctr, (uint16_t)r, s, e);
}

static void rad_to_deg(float node_ang, float trail_len, int dir, uint16_t *out_s, uint16_t *out_e)
{
    float trail_start = node_ang + dir * trail_len;
    float trail_end = node_ang;
    float s = trail_start;
    float e = trail_end;
    int sd;
    int ed;

    if (s > e) {
        float tmp = s;
        s = e;
        e = tmp;
    }
    sd = (int)(s * 180.0f / ORB_PI) % 360;
    ed = (int)(e * 180.0f / ORB_PI) % 360;
    if (sd < 0) sd += 360;
    if (ed < 0) ed += 360;
    if (ed <= sd) ed += 360;
    *out_s = (uint16_t)sd;
    *out_e = (uint16_t)ed;
}

static const float blips[3][2] = {{0.4f, 0.2f}, {0.7f, 0.5f}, {0.3f, 0.8f}};

static void radar_draw(lv_obj_t *screen, const anim_state_cfg_t *cfg, const anim_ctx_t *ctx)
{
    lv_draw_ctx_t *dc = (lv_draw_ctx_t *)ctx->draw_ctx;
    int cx = DESKBOT_DISPLAY_CX;
    int cy = DESKBOT_DISPLAY_CY;
    int r = DESKBOT_DISPLAY_R;
    float t = ctx->t * cfg->speed * 30.0f;
    lv_color_t col = DESKBOT_TO_LV_COLOR(cfg->primary);
    float ang = t * 1.5f;

    (void)screen;

    radar_fill_circle(dc, cx, cy, r, lv_color_hex(0x000000), LV_OPA_COVER);

    for (int i = 0; i < 8; ++i) {
        uint16_t s;
        uint16_t e;
        rad_to_deg(ang, 1.2f, -1, &s, &e);
        radar_draw_arc(dc, cx, cy, (int)(r * 0.88f), (int)(r * 0.04f), s, e, col, (lv_opa_t)(180 - i * 20));
    }

    {
        int tx = cx + (int)(cosf(ang) * r * 0.88f);
        int ty = cy + (int)(sinf(ang) * r * 0.88f);
        radar_draw_line(dc, cx, cy, tx, ty, (int)(r * 0.015f), col, (lv_opa_t)255);
    }

    for (int i = 0; i < 3; ++i) {
        float ba = blips[i][0] * ORB_TWO_PI;
        float br = blips[i][1] * r * 0.82f;
        float diff = fmodf(ang - ba + ORB_TWO_PI, ORB_TWO_PI);

        if (diff < 1.0f) {
            radar_fill_circle(dc, cx + (int)(cosf(ba) * br), cy + (int)(sinf(ba) * br), (int)(r * 0.04f), col, (lv_opa_t)((1.0f - diff) * 255.0f));
        }
    }

    for (int i = 1; i < 4; ++i) {
        radar_draw_arc(dc, cx, cy, (int)(r * 0.3f * i), 1, 0, 360, col, (lv_opa_t)60);
    }
}

static const anim_state_cfg_t radar_states[ANIM_STATE_COUNT] = {
    [ANIM_STATE_IDLE] = {.speed = 0.50f, .primary = DESKBOT_COLOR(0, 196, 144)},
    [ANIM_STATE_LISTENING] = {.speed = 1.40f, .primary = DESKBOT_COLOR(0, 240, 255)},
    [ANIM_STATE_THINKING] = {.speed = 2.80f, .primary = DESKBOT_COLOR(170, 119, 255)},
    [ANIM_STATE_RESPONDING] = {.speed = 0.90f, .primary = DESKBOT_COLOR(0, 255, 212)},
};

const anim_descriptor_t g_anim_radar = {
    .id = ANIM_RADAR,
    .name = "RADAR",
    .description = "Rotating radar sweep with range rings and blip targets.",
    .draw_fn = radar_draw,
    .states = radar_states
};
