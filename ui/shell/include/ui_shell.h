#ifndef UI_SHELL_H
#define UI_SHELL_H

#include <stdbool.h>
#include <stddef.h>

#include "board_profile.h"
#include "drawer.h"
#include "input_platform.h"
#include "presentation.h"
#include "runtime_control.h"

typedef void (*ui_shell_action_cb_t)(void *ctx);
typedef void (*ui_shell_notification_selected_cb_t)(const char *notification_id, void *ctx);

typedef struct {
    ui_shell_action_cb_t on_primary_tap_when_idle;
    ui_shell_action_cb_t on_primary_tap_when_listening;
    ui_shell_action_cb_t on_error_tap;
    ui_shell_notification_selected_cb_t on_notification_selected;
    drawer_volume_cb_t on_volume_changed;
    void *ctx;
} ui_shell_config_t;

typedef struct {
    app_state_t app_state;
    presentation_state_t presentation;
    bool locked;
    bool drawer_visible;
    bool notification_drawer_visible;
    bool runtime_available;
    runtime_status_t runtime_status;
    uint8_t notification_count;
    char notification_summary[96];
    char runtime_status_detail[64];
} ui_shell_status_t;

typedef struct {
    int16_t x;
    int16_t y;
    uint16_t width;
    uint16_t height;
} ui_rect_t;

typedef struct {
    display_shape_t display_shape;
    uint16_t screen_width;
    uint16_t screen_height;
    ui_rect_t primary_visual_rect;
    int16_t state_label_center_x;
    int16_t state_label_baseline_y;
    ui_rect_t drawer_rect;
    int16_t drawer_hidden_y;
    int16_t drawer_open_y;
    uint16_t drawer_trigger_zone;
} ui_shell_layout_t;

bool ui_shell_init(const board_profile_t *board, const ui_shell_config_t *config);
void ui_shell_apply_presentation(presentation_state_t presentation);
void ui_shell_set_locked(bool locked);
bool ui_shell_handle_input_event(const input_event_t *event);
void ui_shell_set_drawer_data(const char *pin, const char *ip, const char *ssid, int volume, int battery_percent);
void ui_shell_set_power_status(bool battery_present, int battery_percent, bool usb_connected);
bool ui_shell_get_drawer_snapshot(drawer_snapshot_t *out_snapshot);
bool ui_shell_get_status(ui_shell_status_t *out_status);
bool ui_shell_get_layout(ui_shell_layout_t *out_layout);
void ui_shell_sync_from_state(app_state_t state);
void ui_shell_sync_from_services(void);
bool ui_shell_notification_upsert(const char *notification_id, const char *summary);
bool ui_shell_notification_remove(const char *notification_id);
void ui_shell_notification_clear_all(void);
void ui_shell_process(void);

#endif
