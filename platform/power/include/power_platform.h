#ifndef POWER_PLATFORM_H
#define POWER_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#include "board_profile.h"

typedef struct {
    bool battery_present;
    int battery_percent;
    bool usb_connected;
    bool external_power_present;
} power_status_t;

typedef enum {
    POWER_WAKE_REASON_UNKNOWN = 0,
    POWER_WAKE_REASON_COLD_BOOT,
    POWER_WAKE_REASON_TIMER,
    POWER_WAKE_REASON_GPIO,
    POWER_WAKE_REASON_TOUCH,
} power_wake_reason_t;

bool power_platform_init(const board_profile_t *board);
bool power_platform_get_status(power_status_t *status);
bool power_platform_get_wake_reason(power_wake_reason_t *out_reason);
bool power_platform_set_battery_percent(int percent);
bool power_platform_set_usb_connected(bool usb_connected);
bool power_platform_prepare_light_sleep(void);
bool power_platform_prepare_deep_sleep(void);
bool power_platform_resume_from_sleep(void);

#endif
