#ifndef LAYOUT_PROFILE_H
#define LAYOUT_PROFILE_H

#include <stdint.h>

typedef enum {
    DISPLAY_SHAPE_RECT = 0,
    DISPLAY_SHAPE_ROUND,
} display_shape_t;

typedef enum {
    VISUAL_ALIGN_TOP_MID = 0,
    VISUAL_ALIGN_CENTER,
} primary_visual_align_t;

typedef enum {
    LAYOUT_PROFILE_ROUND_LCD = 0,
} layout_profile_id_t;

typedef struct {
    layout_profile_id_t id;
    display_shape_t display_shape;
    uint16_t screen_width;
    uint16_t screen_height;
    primary_visual_align_t primary_visual_align;
    uint16_t primary_visual_top_offset;
    uint16_t primary_visual_height;
    uint16_t state_label_bottom_offset;
    uint16_t drawer_height;
    uint16_t drawer_trigger_zone;
} layout_profile_t;

#endif
