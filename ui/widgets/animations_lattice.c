#include "anim_registry.h"

#include <math.h>

#define ORB_TWO_PI 6.28318530f

static void lattice_fill_circle(lv_draw_ctx_t *dc, int cx, int cy, int r, lv_color_t c, lv_opa_t opa)
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

static void lattice_draw(lv_obj_t *screen, const anim_state_cfg_t *cfg, const anim_ctx_t *ctx)
{
    lv_draw_ctx_t *dc = (lv_draw_ctx_t *)ctx->draw_ctx;
    int cx = DESKBOT_DISPLAY_CX;
    int cy = DESKBOT_DISPLAY_CY;
    int r = DESKBOT_DISPLAY_R;
    float t = ctx->t * cfg->speed * 30.0f;
    int g = (int)cfg->params[0];
    float step;
    float s_x;
    float s_y;
    uint8_t r0;
    uint8_t g0;
    uint8_t b0;
    uint8_t r1;
    uint8_t g1;
    uint8_t b1;

    struct wave_t {
        float ox;
        float oy;
        float spd;
        uint8_t r;
        uint8_t g;
        uint8_t b;
    } waves[2];

    (void)screen;

    lattice_fill_circle(dc, cx, cy, r, lv_color_hex(0x000000), LV_OPA_COVER);

    if (g < 3) {
        g = 7;
    }
    step = (r * 1.72f) / g;
    s_x = cx - g / 2.0f * step;
    s_y = cy - g / 2.0f * step;

    r0 = DESKBOT_COLOR_R(cfg->primary);
    g0 = DESKBOT_COLOR_G(cfg->primary);
    b0 = DESKBOT_COLOR_B(cfg->primary);
    r1 = DESKBOT_COLOR_R(cfg->secondary);
    g1 = DESKBOT_COLOR_G(cfg->secondary);
    b1 = DESKBOT_COLOR_B(cfg->secondary);

    waves[0].ox = (float)cx;
    waves[0].oy = (float)cy;
    waves[0].spd = cfg->speed * 1.2f;
    waves[0].r = r0;
    waves[0].g = g0;
    waves[0].b = b0;
    waves[1].ox = cx + cosf(ctx->t * 0.18f) * r * 0.4f;
    waves[1].oy = cy + sinf(ctx->t * 0.18f) * r * 0.4f;
    waves[1].spd = cfg->speed * 0.9f;
    waves[1].r = r1;
    waves[1].g = g1;
    waves[1].b = b1;

    for (int row = 0; row <= g; ++row) {
        for (int col = 0; col <= g; ++col) {
            float px = s_x + col * step;
            float py = s_y + row * step;
            float dx = px - cx;
            float dy = py - cy;
            float amp = 0.0f;
            int tr = 0;
            int tg = 0;
            int tb = 0;

            if (sqrtf(dx * dx + dy * dy) > r * 0.92f) {
                continue;
            }

            for (int w = 0; w < 2; ++w) {
                float wd = sqrtf((px - waves[w].ox) * (px - waves[w].ox) + (py - waves[w].oy) * (py - waves[w].oy));
                float a = 0.5f + 0.5f * sinf((wd / (r * 0.35f) - t * waves[w].spd) * ORB_TWO_PI);
                amp += a;
                tr += waves[w].r * a;
                tg += waves[w].g * a;
                tb += waves[w].b * a;
            }

            amp /= 2.0f;
            lattice_fill_circle(dc, (int)px, (int)py, (int)(1.8f + 3.5f * amp), lv_color_make((uint8_t)(tr / 2), (uint8_t)(tg / 2), (uint8_t)(tb / 2)), (lv_opa_t)((0.25f + 0.65f * amp) * 255.0f));
        }
    }
}

static const anim_state_cfg_t lattice_states[ANIM_STATE_COUNT] = {
    [ANIM_STATE_IDLE] = {.speed = 0.50f, .primary = DESKBOT_COLOR(0, 196, 180), .secondary = DESKBOT_COLOR(74, 111, 255), .params = {7.0f}},
    [ANIM_STATE_LISTENING] = {.speed = 1.40f, .primary = DESKBOT_COLOR(0, 240, 255), .secondary = DESKBOT_COLOR(0, 102, 255), .params = {7.0f}},
    [ANIM_STATE_THINKING] = {.speed = 2.50f, .primary = DESKBOT_COLOR(170, 119, 255), .secondary = DESKBOT_COLOR(102, 51, 204), .params = {7.0f}},
    [ANIM_STATE_RESPONDING] = {.speed = 0.80f, .primary = DESKBOT_COLOR(0, 255, 212), .secondary = DESKBOT_COLOR(0, 196, 180), .params = {7.0f}},
};

const anim_descriptor_t g_anim_lattice = {
    .id = ANIM_LATTICE,
    .name = "LATTICE",
    .description = "Interference wave lattice across a dot grid.",
    .draw_fn = lattice_draw,
    .states = lattice_states
};
