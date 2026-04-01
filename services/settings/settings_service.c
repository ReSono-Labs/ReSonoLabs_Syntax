#include "settings_service.h"

#include "storage_platform.h"

#define SETTINGS_NAMESPACE "settings"
#define KEY_VOLUME "volume"
#define KEY_BRIGHTNESS "brightness"
#define DEFAULT_VOLUME 70
#define DEFAULT_BRIGHTNESS 55

static uint8_t clamp_percent(uint8_t value, uint8_t min_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > 100U) {
        return 100U;
    }
    return value;
}

bool settings_service_init(void)
{
    return storage_platform_init();
}

bool settings_service_get_volume(uint8_t *out_volume)
{
    int32_t stored_value;

    if (out_volume == 0 || !settings_service_init()) {
        return false;
    }
    if (!storage_platform_get_i32(SETTINGS_NAMESPACE, KEY_VOLUME, &stored_value)) {
        stored_value = DEFAULT_VOLUME;
    }

    *out_volume = clamp_percent((uint8_t)stored_value, 0U);
    return true;
}

bool settings_service_set_volume(uint8_t volume)
{
    if (!settings_service_init()) {
        return false;
    }
    return storage_platform_set_i32(SETTINGS_NAMESPACE, KEY_VOLUME, clamp_percent(volume, 0U));
}

bool settings_service_get_brightness(uint8_t *out_brightness)
{
    int32_t stored_value;

    if (out_brightness == 0 || !settings_service_init()) {
        return false;
    }
    if (!storage_platform_get_i32(SETTINGS_NAMESPACE, KEY_BRIGHTNESS, &stored_value)) {
        stored_value = DEFAULT_BRIGHTNESS;
    }

    *out_brightness = clamp_percent((uint8_t)stored_value, 5U);
    return true;
}

bool settings_service_set_brightness(uint8_t brightness)
{
    if (!settings_service_init()) {
        return false;
    }
    return storage_platform_set_i32(SETTINGS_NAMESPACE, KEY_BRIGHTNESS, clamp_percent(brightness, 5U));
}
