#include "drawer.h"

#include <stddef.h>
#include <string.h>

static bool s_visible;
static drawer_config_t s_config;
static drawer_snapshot_t s_snapshot;

static int clamp_volume(int volume)
{
    if (volume < 0) {
        return 0;
    }
    if (volume > 100) {
        return 100;
    }
    return volume;
}

static void copy_text(char *dst, size_t dst_len, const char *src, const char *fallback)
{
    const char *value = src;

    if (dst == NULL || dst_len == 0) {
        return;
    }

    if (value == NULL || value[0] == '\0') {
        value = fallback;
    }

    if (value == NULL) {
        value = "";
    }

    strncpy(dst, value, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

bool drawer_init(const drawer_config_t *config)
{
    memset(&s_config, 0, sizeof(s_config));
    memset(&s_snapshot, 0, sizeof(s_snapshot));

    if (config != NULL) {
        s_config = *config;
    }

    s_visible = false;
    copy_text(s_snapshot.pin, sizeof(s_snapshot.pin), NULL, "------");
    copy_text(s_snapshot.ip, sizeof(s_snapshot.ip), NULL, "---");
    copy_text(s_snapshot.ssid, sizeof(s_snapshot.ssid), NULL, "---");
    s_snapshot.volume = clamp_volume(s_config.initial_volume);
    s_snapshot.battery_percent = -1;
    s_snapshot.battery_present = false;
    s_snapshot.usb_connected = false;
    return true;
}

void drawer_open(void)
{
    s_visible = true;
}

void drawer_close(void)
{
    s_visible = false;
}

void drawer_toggle(void)
{
    if (s_visible) {
        drawer_close();
        return;
    }

    drawer_open();
}

bool drawer_is_visible(void)
{
    return s_visible;
}

int drawer_get_volume(void)
{
    return s_snapshot.volume;
}

bool drawer_set_volume(int volume)
{
    int clamped = clamp_volume(volume);

    if (s_snapshot.volume == clamped) {
        return true;
    }

    s_snapshot.volume = clamped;
    if (s_config.on_volume_changed != NULL) {
        s_config.on_volume_changed(clamped, s_config.ctx);
    }
    return true;
}

bool drawer_get_snapshot(drawer_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return false;
    }

    *out_snapshot = s_snapshot;
    return true;
}

void drawer_update(const char *pin, const char *ip, const char *ssid, int volume, int battery_percent)
{
    copy_text(s_snapshot.pin, sizeof(s_snapshot.pin), pin, "------");
    copy_text(s_snapshot.ip, sizeof(s_snapshot.ip), ip, "---");
    copy_text(s_snapshot.ssid, sizeof(s_snapshot.ssid), ssid, "---");
    s_snapshot.volume = clamp_volume(volume);
    s_snapshot.battery_percent = battery_percent;
}

void drawer_set_power_status(bool battery_present, int battery_percent, bool usb_connected)
{
    s_snapshot.battery_present = battery_present;
    s_snapshot.battery_percent = battery_percent;
    s_snapshot.usb_connected = usb_connected;
}
