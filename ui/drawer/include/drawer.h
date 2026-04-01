#ifndef DRAWER_H
#define DRAWER_H

#include <stdbool.h>
#include <stdint.h>

typedef void (*drawer_volume_cb_t)(int volume, void *ctx);

typedef struct {
    drawer_volume_cb_t on_volume_changed;
    void *ctx;
    int initial_volume;
} drawer_config_t;

typedef struct {
    char pin[8];
    char ip[64];
    char ssid[64];
    int volume;
    int battery_percent;
    bool battery_present;
    bool usb_connected;
} drawer_snapshot_t;

bool drawer_init(const drawer_config_t *config);
void drawer_open(void);
void drawer_close(void);
void drawer_toggle(void);
bool drawer_is_visible(void);
int drawer_get_volume(void);
bool drawer_set_volume(int volume);
bool drawer_get_snapshot(drawer_snapshot_t *out_snapshot);
void drawer_update(const char *pin, const char *ip, const char *ssid, int volume, int battery_percent);
void drawer_set_power_status(bool battery_present, int battery_percent, bool usb_connected);

#endif
