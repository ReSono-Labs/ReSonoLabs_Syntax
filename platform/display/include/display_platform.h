#ifndef DISPLAY_PLATFORM_H
#define DISPLAY_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#include "board_profile.h"

typedef struct {
    uint16_t width;
    uint16_t height;
    display_tech_t tech;
    display_shape_t shape;
} display_info_t;

bool display_platform_init(const board_profile_t *board);
bool display_platform_sleep(void);
bool display_platform_wake(void);
display_info_t display_platform_get_info(void);
bool display_platform_flush_rect(int x1, int y1, int x2, int y2, const void *color_data);

#endif
