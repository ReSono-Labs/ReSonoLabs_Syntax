#ifndef ORB_WIDGET_H
#define ORB_WIDGET_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "orb_service.h"
#include "presentation.h"

typedef struct {
    bool initialized;
    presentation_visual_t visual;
    orb_config_t config;
    bool has_config;
} orb_widget_snapshot_t;

bool orb_widget_init(void);
bool orb_widget_bind_lvgl(lv_obj_t *parent, int16_t x, int16_t y, uint16_t width, uint16_t height);
void orb_widget_apply_presentation(presentation_visual_t visual);
void orb_widget_apply_config(const orb_config_t *config);
void orb_widget_set_level(float level_0_to_1);
bool orb_widget_get_snapshot(orb_widget_snapshot_t *out_snapshot);
size_t orb_widget_theme_count(void);
const char *orb_widget_theme_name(uint8_t theme_id);
bool orb_widget_theme_apply_defaults(uint8_t theme_id, orb_config_t *config);

#endif
