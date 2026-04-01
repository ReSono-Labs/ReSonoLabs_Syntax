#ifndef BOARD_PROFILE_H
#define BOARD_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#include "layout_profile.h"

typedef enum {
    DISPLAY_TECH_OLED = 0,
    DISPLAY_TECH_LCD,
} display_tech_t;

typedef enum {
    BOARD_TARGET_UNKNOWN = 0,
    BOARD_TARGET_ESP32,
    BOARD_TARGET_ESP32S3,
    BOARD_TARGET_ESP32C4,
    BOARD_TARGET_ESP32P4,
} board_target_t;

typedef struct {
    bool has_touch;
    bool has_microphone;
    bool has_speaker;
    bool has_battery_monitor;
    bool has_display_backlight_control;
    bool supports_light_sleep;
    bool supports_deep_sleep;
    bool has_wifi;
    bool has_bluetooth;
    bool has_usb_device;
    bool has_usb_power_sense;
} board_capabilities_t;

typedef struct {
    const char *board_id;
    const char *board_label;
    board_target_t target;
    display_tech_t display_tech;
    layout_profile_t layout;
    board_capabilities_t capabilities;
} board_profile_t;

#endif
