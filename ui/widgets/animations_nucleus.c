#include "anim_registry.h"

#include <math.h>

#define ORB_PI 3.14159265f

static const float BASE_RADII[3] = {0.70f, 0.50f, 0.31f};

static void fill_circle(lv_draw_ctx_t *dc, int cx, int cy, int r, deskbot_color_t c, uint8_t opa)
{
    lv_draw_rect_dsc_t d;
    lv_area_t a;

    if (r <= 0) {
        return;
    }

    lv_draw_rect_dsc_init(&d);
    d.bg_color = DESKBOT_TO_LV_COLOR(c);
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

static void draw_line_seg(lv_draw_ctx_t *dc, int x1, int y1, int x2, int y2, int w, deskbot_color_t c, uint8_t opa)
{
    lv_draw_line_dsc_t d;
    lv_point_t p1;
    lv_point_t p2;

    lv_draw_line_dsc_init(&d);
    d.color = DESKBOT_TO_LV_COLOR(c);
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

static void draw_arc_segment(lv_draw_ctx_t *dc, int cx, int cy, int r, int w, uint16_t start_deg, uint16_t end_deg, deskbot_color_t c, uint8_t opa)
{
    lv_draw_arc_dsc_t d;
    lv_point_t center;

    if (r <= 0 || w <= 0) {
        return;
    }

    lv_draw_arc_dsc_init(&d);
    d.color = DESKBOT_TO_LV_COLOR(c);
    d.opa = opa;
    d.width = (uint16_t)w;
    d.rounded = 1;
    center.x = (lv_coord_t)cx;
    center.y = (lv_coord_t)cy;
    lv_draw_arc(dc, &d, &center, (uint16_t)r, start_deg, end_deg);
}

static void rad_trail_to_deg(float node_ang, float trail_len, int dir, uint16_t *out_start, uint16_t *out_end)
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
    *out_start = (uint16_t)sd;
    *out_end = (uint16_t)ed;
}

static void nucleus_draw(lv_obj_t *screen, const anim_state_cfg_t *cfg, const anim_ctx_t *ctx)
{
    int cx = DESKBOT_DISPLAY_CX;
    int cy = DESKBOT_DISPLAY_CY;
    int r = DESKBOT_DISPLAY_R;
    float t = ctx->t * cfg->speed * 30.0f;
    float breathe = t * 1.5f;
    int nx[3];
    int ny[3];
    deskbot_color_t colors[3] = {cfg->primary, cfg->secondary, cfg->tertiary};

    (void)screen;

    for (int i = 0; i < 3; ++i) {
        float speed_mul = cfg->params[i];
        float trail_len = cfg->params[i + 3];
        float node_r = cfg->params[6];
        float orbit_scale = cfg->params[11];
        int rr = (int)(r * BASE_RADII[i] * orbit_scale);

        if (trail_len > 0.05f) {
            int trail_dir = (speed_mul >= 0.0f) ? -1 : 1;
            float node_ang = t * speed_mul;
            uint16_t t_start;
            uint16_t t_end;

            rad_trail_to_deg(node_ang, trail_len, trail_dir, &t_start, &t_end);
            draw_arc_segment((lv_draw_ctx_t *)ctx->draw_ctx, cx, cy, rr, (int)(r * 0.032f), t_start, t_end, colors[i], 180);
        }

        nx[i] = cx + (int)(cosf(t * speed_mul) * rr);
        ny[i] = cy + (int)(sinf(t * speed_mul) * rr);

        {
            int nr = (int)(r * node_r * (1.0f + 0.15f * sinf(breathe * 2.5f + i * 1.3f)));
            fill_circle((lv_draw_ctx_t *)ctx->draw_ctx, nx[i], ny[i], nr, colors[i], 230);
        }
    }

    for (int a = 0; a < 3; ++a) {
        for (int b = a + 1; b < 3; ++b) {
            int dx = nx[a] - nx[b];
            int dy = ny[a] - ny[b];
            int dist = (int)sqrtf((float)(dx * dx + dy * dy));
            int thr = (int)(r * cfg->params[7]);

            if (dist < thr && dist > 0) {
                float strength = 1.0f - (float)dist / (float)thr;
                draw_line_seg((lv_draw_ctx_t *)ctx->draw_ctx, nx[a], ny[a], nx[b], ny[b], (int)(r * 0.010f * strength), DESKBOT_COLOR(255, 255, 255), (uint8_t)(strength * 160.0f));
            }
        }
    }

    {
        float core_pulse = 1.0f + 0.22f * sinf(breathe * cfg->params[9]);
        int core_r = (int)(r * cfg->params[8] * core_pulse);
        uint8_t core_opa = (uint8_t)(255.0f * cfg->params[10]);
        fill_circle((lv_draw_ctx_t *)ctx->draw_ctx, cx, cy, core_r, DESKBOT_COLOR(255, 255, 255), core_opa);
    }
}

static const anim_state_cfg_t nucleus_states[ANIM_STATE_COUNT] = {
    [ANIM_STATE_IDLE] = {.speed = 0.030f, .primary = DESKBOT_COLOR(0, 196, 180), .secondary = DESKBOT_COLOR(74, 111, 255), .tertiary = DESKBOT_COLOR(59, 59, 138), .intensity = 0.1f, .params = {1.0f, -0.6f, 1.4f, 2.2f, 1.8f, 1.5f, 0.058f, 0.18f, 0.10f, 1.0f, 0.8f, 1.0f}},
    [ANIM_STATE_LISTENING] = {.speed = 0.065f, .primary = DESKBOT_COLOR(0, 240, 255), .secondary = DESKBOT_COLOR(0, 170, 255), .tertiary = DESKBOT_COLOR(0, 102, 255), .intensity = 0.5f, .params = {1.0f, 0.77f, 1.27f, 0.55f, 0.45f, 0.35f, 0.062f, 0.22f, 0.10f, 2.0f, 0.9f, 1.0f}},
    [ANIM_STATE_THINKING] = {.speed = 0.045f, .primary = DESKBOT_COLOR(102, 51, 204), .secondary = DESKBOT_COLOR(170, 119, 255), .tertiary = DESKBOT_COLOR(204, 170, 255), .intensity = 0.8f, .params = {0.22f, -2.8f, 3.6f, 0.08f, 0.12f, 0.06f, 0.055f, 0.35f, 0.08f, 3.0f, 0.7f, 1.0f}},
    [ANIM_STATE_RESPONDING] = {.speed = 0.050f, .primary = DESKBOT_COLOR(0, 255, 212), .secondary = DESKBOT_COLOR(0, 196, 180), .tertiary = DESKBOT_COLOR(0, 158, 142), .intensity = 1.0f, .params = {1.0f, 0.77f, 1.2f, 2.8f, 2.4f, 2.0f, 0.065f, 0.25f, 0.12f, 1.5f, 1.0f, 1.0f}},
};

const anim_descriptor_t g_anim_nucleus = {
    .id = ANIM_NUCLEUS,
    .name = "NUCLEUS",
    .description = "The default 4-state AI visualization.",
    .draw_fn = nucleus_draw,
    .states = nucleus_states
};
