#ifndef S3_1_85C_BOARD_H
#define S3_1_85C_BOARD_H

#include "board_profile.h"

static const board_profile_t g_s3_1_85c_board = {
    .board_id = "s3_1_85c",
    .board_label = "ESP32-S3 1.85C Round Display",
    .target = BOARD_TARGET_ESP32S3,
    .display_tech = DISPLAY_TECH_LCD,
    .layout = {
        .id = LAYOUT_PROFILE_ROUND_LCD,
        .display_shape = DISPLAY_SHAPE_ROUND,
        .screen_width = 360,
        .screen_height = 360,
        .primary_visual_align = VISUAL_ALIGN_CENTER,
        .primary_visual_top_offset = 0,
        .primary_visual_height = 360,
        .state_label_bottom_offset = 20,
        .drawer_height = 360,
        .drawer_trigger_zone = 100,
    },
    .capabilities = {
        .has_touch = true,
        .has_microphone = true,
        .has_speaker = true,
        .has_battery_monitor = false,
        .has_display_backlight_control = true,
        .supports_light_sleep = false,
        .supports_deep_sleep = true,
        .has_wifi = true,
        .has_bluetooth = true,
        .has_usb_device = true,
        .has_usb_power_sense = false,
    },
};

#endif
