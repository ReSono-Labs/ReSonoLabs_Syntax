#include "power_platform.h"

#include "display_platform.h"
#include "esp_sleep.h"

static board_profile_t s_board;
static power_status_t s_status;
static power_wake_reason_t s_wake_reason = POWER_WAKE_REASON_UNKNOWN;
static bool s_initialized;

static power_wake_reason_t read_wake_reason(void)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    if (cause == ESP_SLEEP_WAKEUP_UNDEFINED) {
        return POWER_WAKE_REASON_COLD_BOOT;
    }
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        return POWER_WAKE_REASON_TIMER;
    }
    if (cause == ESP_SLEEP_WAKEUP_GPIO ||
        cause == ESP_SLEEP_WAKEUP_EXT0 ||
        cause == ESP_SLEEP_WAKEUP_EXT1) {
        return POWER_WAKE_REASON_GPIO;
    }
#if SOC_PM_SUPPORT_TOUCH_SENSOR_WAKEUP
    if (cause == ESP_SLEEP_WAKEUP_TOUCHPAD) {
        return POWER_WAKE_REASON_TOUCH;
    }
#endif

    return POWER_WAKE_REASON_UNKNOWN;
}

static int clamp_battery_percent(int percent)
{
    if (percent < 0) {
        return -1;
    }
    if (percent > 100) {
        return 100;
    }
    return percent;
}

bool power_platform_init(const board_profile_t *board)
{
    if (board == 0) {
        return false;
    }

    s_board = *board;
    s_status.battery_present = board->capabilities.has_battery_monitor;
    s_status.battery_percent = -1;
    s_status.usb_connected = false;
    s_status.external_power_present = board->capabilities.has_usb_power_sense;
    s_wake_reason = read_wake_reason();
    s_initialized = true;
    return true;
}

bool power_platform_get_status(power_status_t *status)
{
    if (status == 0) {
        return false;
    }

    *status = s_status;
    return true;
}

bool power_platform_get_wake_reason(power_wake_reason_t *out_reason)
{
    if (out_reason == 0 || !s_initialized) {
        return false;
    }

    *out_reason = s_wake_reason;
    return true;
}

bool power_platform_set_battery_percent(int percent)
{
    if (!s_initialized || !s_status.battery_present) {
        return false;
    }

    s_status.battery_percent = clamp_battery_percent(percent);
    return true;
}

bool power_platform_set_usb_connected(bool usb_connected)
{
    if (!s_initialized) {
        return false;
    }

    s_status.usb_connected = usb_connected;
    s_status.external_power_present = usb_connected;
    return true;
}

bool power_platform_prepare_light_sleep(void)
{
    if (!s_initialized || !s_board.capabilities.supports_light_sleep) {
        return false;
    }

    return display_platform_sleep();
}

bool power_platform_prepare_deep_sleep(void)
{
    if (!s_initialized || !s_board.capabilities.supports_deep_sleep) {
        return false;
    }

    return display_platform_sleep();
}

bool power_platform_resume_from_sleep(void)
{
    if (!s_initialized) {
        return false;
    }

    s_wake_reason = read_wake_reason();
    return display_platform_wake();
}
