#include "ui_shell.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "display_platform.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "input_platform.h"
#include "lvgl.h"
#include "notification_tray.h"
#include "orb_widget.h"
#include "power_platform.h"
#include "runtime_control.h"
#include "security_auth.h"
#include "settings_service.h"
#include "wifi_setup_service.h"

typedef struct {
    board_profile_t board;
    ui_shell_config_t config;
    ui_shell_status_t status;
    bool initialized;
} ui_shell_context_t;

static ui_shell_context_t s_shell;
static const char *TAG = "ui_shell";
static lv_obj_t *s_screen;
static lv_obj_t *s_state_label;
static lv_obj_t *s_runtime_label;
static lv_obj_t *s_drawer;
static lv_obj_t *s_drawer_pin;
static lv_obj_t *s_drawer_ssid;
static lv_obj_t *s_drawer_ip;
static lv_obj_t *s_drawer_volume;
static lv_obj_t *s_drawer_power;
static lv_obj_t *s_drawer_title_pin;
static lv_obj_t *s_drawer_title_ssid;
static lv_obj_t *s_drawer_title_ip;
static lv_obj_t *s_drawer_title_volume;
static lv_obj_t *s_drawer_title_power;
static lv_obj_t *s_drawer_volume_slider;
static lv_obj_t *s_notification_drawer;
static lv_obj_t *s_notification_title;
static lv_obj_t *s_notification_hint;
static lv_obj_t *s_notification_empty;
static lv_obj_t *s_notification_rows[NOTIFICATION_TRAY_MAX_ITEMS];
static lv_obj_t *s_notification_row_labels[NOTIFICATION_TRAY_MAX_ITEMS];
static int32_t s_drawer_offset;
static int32_t s_notification_drawer_offset;
static bool s_drawer_volume_interaction;
static lv_area_t s_drawer_volume_slider_hitbox;
static lv_area_t s_notification_row_hitboxes[NOTIFICATION_TRAY_MAX_ITEMS];
static lv_disp_draw_buf_t s_draw_buf;
static lv_disp_drv_t s_disp_drv;
static esp_timer_handle_t s_lvgl_tick_timer;
static SemaphoreHandle_t s_lvgl_mutex;
static bool s_lvgl_ready;

#define LVGL_MAX_DRAW_BUFFER_PIXELS (480U * 20U)
static lv_color_t s_buf1[LVGL_MAX_DRAW_BUFFER_PIXELS];
static lv_color_t s_buf2[LVGL_MAX_DRAW_BUFFER_PIXELS];

static void shell_apply_drawer_visibility(void);
static void shell_refresh_labels(void);

#define SHELL_STATUS_DEFAULT_COLOR 0xf8fafc
#define SHELL_STATUS_NOTIFICATION_COLOR 0x4ade80
#define SHELL_RUNTIME_DEFAULT_COLOR 0x94a3b8
#define SHELL_NOTIFICATION_VISIBLE_ROWS 3

static lv_coord_t shell_notification_drawer_height(const ui_shell_layout_t *layout)
{
    if (layout == NULL) {
        return 0;
    }
    return (lv_coord_t)layout->screen_height;
}

static void shell_drawer_set_offset(void *obj, int32_t offset)
{
    lv_obj_t *drawer = (lv_obj_t *)obj;

    s_drawer_offset = offset;
    if (drawer != NULL) {
        lv_obj_set_y(drawer, (lv_coord_t)(offset - lv_obj_get_height(drawer)));
    }
}

static void shell_notification_drawer_set_offset(void *obj, int32_t offset)
{
    lv_obj_t *drawer = (lv_obj_t *)obj;
    display_info_t info = display_platform_get_info();

    s_notification_drawer_offset = offset;
    if (drawer != NULL) {
        lv_coord_t y = (lv_coord_t)((int32_t)info.height - offset);
        lv_obj_set_y(drawer, y);
    }
}

static void shell_set_label_text(lv_obj_t *label, const char *text)
{
    if (label == NULL) {
        return;
    }

    lv_label_set_text(label, text != NULL ? text : "");
}

static bool shell_point_in_area(int16_t x, int16_t y, const lv_area_t *area)
{
    return area != NULL && x >= area->x1 && x <= area->x2 && y >= area->y1 && y <= area->y2;
}

static bool shell_notification_index_from_point(const ui_shell_layout_t *layout,
                                                int16_t x,
                                                int16_t y,
                                                size_t item_count,
                                                size_t *out_index)
{
    const int16_t drawer_top = (int16_t)(layout->screen_height - shell_notification_drawer_height(layout));
    const int16_t row_left = 20;
    const int16_t row_right = (int16_t)(layout->screen_width - 20);
    const int16_t row_top = (int16_t)(drawer_top + 84);
    const int16_t row_pitch = 56;
    const int16_t row_height = 48;
    int16_t rel_y;
    size_t index;

    if (out_index == NULL || item_count == 0U) {
        return false;
    }
    if (x < row_left || x > row_right || y < row_top) {
        return false;
    }

    rel_y = (int16_t)(y - row_top);
    index = (size_t)(rel_y / row_pitch);
    if (index >= item_count || index >= NOTIFICATION_TRAY_MAX_ITEMS) {
        return false;
    }
    if (rel_y > (int16_t)((index * row_pitch) + row_height)) {
        return false;
    }

    *out_index = index;
    return true;
}

static int shell_slider_value_from_x(int16_t x, const lv_area_t *area)
{
    int width;
    int rel_x;

    if (area == NULL) {
        return 0;
    }

    width = area->x2 - area->x1;
    if (width <= 0) {
        return 0;
    }

    rel_x = x - area->x1;
    if (rel_x < 0) {
        rel_x = 0;
    }
    if (rel_x > width) {
        rel_x = width;
    }

    return (rel_x * 100) / width;
}

static lv_obj_t *shell_make_drawer_title(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, x, y);
    return label;
}

static lv_obj_t *shell_make_drawer_value(lv_obj_t *parent,
                                         const char *text,
                                         lv_coord_t x,
                                         lv_coord_t y,
                                         lv_coord_t width,
                                         const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_color(label, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_align(label, LV_ALIGN_TOP_MID, x, y);
    return label;
}

static lv_obj_t *shell_make_notification_row(lv_obj_t *parent, lv_coord_t y)
{
    lv_obj_t *row = lv_obj_create(parent);

    lv_obj_set_size(row, 296, 40);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x101a16), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(0x1f3b2f), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_radius(row, 14, 0);
    lv_obj_set_style_outline_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_pad_left(row, 14, 0);
    lv_obj_set_style_pad_right(row, 14, 0);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_set_style_pad_bottom(row, 8, 0);
    return row;
}

static bool shell_create_drawer_ui(const ui_shell_layout_t *layout)
{
    const lv_coord_t width = 300;
    const lv_coord_t slider_width = 240;
    const lv_coord_t slider_y = 235;

    s_drawer = lv_obj_create(s_screen);
    if (s_drawer == NULL) {
        return false;
    }

    lv_obj_set_size(s_drawer, layout->drawer_rect.width, layout->drawer_rect.height);
    s_drawer_offset = 0;
    lv_obj_set_pos(s_drawer, layout->drawer_rect.x, layout->drawer_hidden_y);
    lv_obj_clear_flag(s_drawer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_drawer, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_drawer, lv_color_make(12, 12, 24), 0);
    lv_obj_set_style_bg_opa(s_drawer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_drawer, 0, 0);
    lv_obj_set_style_outline_width(s_drawer, 0, 0);
    lv_obj_set_style_shadow_width(s_drawer, 0, 0);
    lv_obj_set_style_radius(s_drawer, 0, 0);
    lv_obj_set_style_pad_all(s_drawer, 0, 0);

    {
        lv_obj_t *handle = lv_obj_create(s_drawer);
        lv_obj_set_size(handle, 56, 5);
        lv_obj_align(handle, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_bg_color(handle, lv_color_hex(0x334155), 0);
        lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(handle, 0, 0);
        lv_obj_set_style_radius(handle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_outline_width(handle, 0, 0);
        lv_obj_set_style_shadow_width(handle, 0, 0);
    }

    s_drawer_title_pin = shell_make_drawer_title(s_drawer, "SECURITY", 0, 15);
    s_drawer_pin = shell_make_drawer_value(s_drawer, "------", 0, 32, width, &lv_font_montserrat_16);

    s_drawer_title_ssid = shell_make_drawer_title(s_drawer, "NETWORK", 0, 85);
    s_drawer_ssid = shell_make_drawer_value(s_drawer, "---", 0, 108, width, &lv_font_montserrat_14);

    s_drawer_title_ip = shell_make_drawer_title(s_drawer, "IP ADDRESS", 0, 145);
    s_drawer_ip = shell_make_drawer_value(s_drawer, "---", 0, 168, width, &lv_font_montserrat_14);

    s_drawer_title_volume = shell_make_drawer_title(s_drawer, "VOLUME", 0, 210);
    s_drawer_volume_slider = lv_slider_create(s_drawer);
    lv_slider_set_range(s_drawer_volume_slider, 0, 100);
    lv_slider_set_value(s_drawer_volume_slider, 70, LV_ANIM_OFF);
    lv_obj_set_size(s_drawer_volume_slider, slider_width, 20);
    lv_obj_align(s_drawer_volume_slider, LV_ALIGN_TOP_MID, 0, slider_y);
    lv_obj_set_style_bg_color(s_drawer_volume_slider, lv_color_make(35, 35, 60), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_drawer_volume_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_drawer_volume_slider, lv_color_make(70, 110, 210), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_drawer_volume_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_drawer_volume_slider, lv_color_make(130, 170, 255), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_drawer_volume_slider, 6, LV_PART_KNOB);
    s_drawer_volume = NULL;

    s_drawer_title_power = shell_make_drawer_title(s_drawer, "POWER", 0, 258);
    s_drawer_power = shell_make_drawer_value(s_drawer, "USB off", 0, 279, width, &lv_font_montserrat_14);

    {
        lv_obj_t *hint = lv_label_create(s_drawer);
        lv_label_set_text(hint, LV_SYMBOL_UP " swipe up to close");
        lv_obj_set_style_text_color(hint, lv_color_hex(0x64748b), 0);
        lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
        lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -18);
    }

    s_drawer_volume_slider_hitbox.x1 = (layout->drawer_rect.width - slider_width) / 2;
    s_drawer_volume_slider_hitbox.x2 = s_drawer_volume_slider_hitbox.x1 + slider_width;
    s_drawer_volume_slider_hitbox.y1 = slider_y;
    s_drawer_volume_slider_hitbox.y2 = slider_y + 20;

    return true;
}

static bool shell_create_notification_drawer_ui(const ui_shell_layout_t *layout)
{
    size_t index;
    const lv_coord_t row_start_y = 84;
    const lv_coord_t row_gap = 56;
    const lv_coord_t drawer_height = shell_notification_drawer_height(layout);

    s_notification_drawer = lv_obj_create(s_screen);
    if (s_notification_drawer == NULL) {
        return false;
    }

    lv_obj_set_size(s_notification_drawer, layout->screen_width, drawer_height);
    s_notification_drawer_offset = 0;
    lv_obj_set_pos(s_notification_drawer, 0, (lv_coord_t)(layout->screen_height));
    lv_obj_clear_flag(s_notification_drawer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_notification_drawer, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(s_notification_drawer, lv_color_hex(0x07110d), 0);
    lv_obj_set_style_bg_opa(s_notification_drawer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_notification_drawer, 0, 0);
    lv_obj_set_style_outline_width(s_notification_drawer, 0, 0);
    lv_obj_set_style_shadow_width(s_notification_drawer, 0, 0);
    lv_obj_set_style_radius(s_notification_drawer, 0, 0);
    lv_obj_set_style_pad_all(s_notification_drawer, 0, 0);
    lv_obj_move_foreground(s_notification_drawer);

    {
        lv_obj_t *handle = lv_obj_create(s_notification_drawer);
        lv_obj_set_size(handle, 56, 5);
        lv_obj_align(handle, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_set_style_bg_color(handle, lv_color_hex(0x2f5f48), 0);
        lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(handle, 0, 0);
        lv_obj_set_style_radius(handle, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_outline_width(handle, 0, 0);
        lv_obj_set_style_shadow_width(handle, 0, 0);
    }

    s_notification_title = lv_label_create(s_notification_drawer);
    lv_label_set_text(s_notification_title, "RESULTS");
    lv_obj_set_style_text_color(s_notification_title, lv_color_hex(0x86efac), 0);
    lv_obj_set_style_text_font(s_notification_title, &lv_font_montserrat_16, 0);
    lv_obj_align(s_notification_title, LV_ALIGN_TOP_LEFT, 24, 24);

    s_notification_hint = lv_label_create(s_notification_drawer);
    lv_label_set_text(s_notification_hint, "Tap a result to open");
    lv_obj_set_style_text_color(s_notification_hint, lv_color_hex(0x6b8f7a), 0);
    lv_obj_set_style_text_font(s_notification_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(s_notification_hint, LV_ALIGN_TOP_RIGHT, -24, 24);

    s_notification_empty = lv_label_create(s_notification_drawer);
    lv_label_set_text(s_notification_empty, "No results yet");
    lv_obj_set_style_text_color(s_notification_empty, lv_color_hex(0x6b7280), 0);
    lv_obj_set_style_text_font(s_notification_empty, &lv_font_montserrat_14, 0);
    lv_obj_align(s_notification_empty, LV_ALIGN_CENTER, 0, 0);

    for (index = 0; index < SHELL_NOTIFICATION_VISIBLE_ROWS; ++index) {
        s_notification_rows[index] = shell_make_notification_row(
            s_notification_drawer,
            (lv_coord_t)(row_start_y + (index * row_gap)));
        s_notification_row_labels[index] = lv_label_create(s_notification_rows[index]);
        lv_obj_set_width(s_notification_row_labels[index], 248);
        lv_label_set_long_mode(s_notification_row_labels[index], LV_LABEL_LONG_DOT);
        lv_label_set_text(s_notification_row_labels[index], "");
        lv_obj_set_style_text_color(s_notification_row_labels[index], lv_color_hex(0xf0fdf4), 0);
        lv_obj_set_style_text_font(s_notification_row_labels[index], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_align(s_notification_row_labels[index], LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_align(s_notification_row_labels[index], LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_flag(s_notification_rows[index], LV_OBJ_FLAG_HIDDEN);
        memset(&s_notification_row_hitboxes[index], 0, sizeof(s_notification_row_hitboxes[index]));
    }
    for (; index < NOTIFICATION_TRAY_MAX_ITEMS; ++index) {
        s_notification_rows[index] = NULL;
        s_notification_row_labels[index] = NULL;
        memset(&s_notification_row_hitboxes[index], 0, sizeof(s_notification_row_hitboxes[index]));
    }

    return true;
}

static bool shell_lvgl_lock(TickType_t timeout)
{
    return s_lvgl_mutex != NULL && xSemaphoreTakeRecursive(s_lvgl_mutex, timeout) == pdTRUE;
}

static void shell_lvgl_unlock(void)
{
    if (s_lvgl_mutex != NULL) {
        xSemaphoreGiveRecursive(s_lvgl_mutex);
    }
}

static void shell_lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(10);
}

static void shell_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    (void)display_platform_flush_rect(area->x1, area->y1, area->x2, area->y2, color_map);
    lv_disp_flush_ready(drv);
}

static bool shell_lvgl_init_display(void)
{
    display_info_t info = display_platform_get_info();
    size_t buf_pixels;
    const esp_timer_create_args_t tick_args = {
        .callback = shell_lvgl_tick_cb,
        .name = "lvgl_tick",
    };

    if (s_lvgl_ready) {
        ESP_LOGI(TAG, "LVGL display already initialized");
        return true;
    }

    lv_init();
    buf_pixels = (size_t)info.width * 20U;

    if (buf_pixels > LVGL_MAX_DRAW_BUFFER_PIXELS) {
        ESP_LOGE(TAG, "LVGL draw buffer allocation failed");
        return false;
    }
    memset(s_buf1, 0, buf_pixels * sizeof(lv_color_t));
    memset(s_buf2, 0, buf_pixels * sizeof(lv_color_t));

    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, s_buf2, (uint32_t)buf_pixels);
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = info.width;
    s_disp_drv.ver_res = info.height;
    s_disp_drv.flush_cb = shell_lvgl_flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);

    if (esp_timer_create(&tick_args, &s_lvgl_tick_timer) != ESP_OK) {
        ESP_LOGE(TAG, "LVGL tick timer create failed");
        return false;
    }
    if (esp_timer_start_periodic(s_lvgl_tick_timer, 10000) != ESP_OK) {
        ESP_LOGE(TAG, "LVGL tick timer start failed");
        return false;
    }
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (s_lvgl_mutex == NULL) {
        ESP_LOGE(TAG, "LVGL mutex create failed");
        return false;
    }

    s_lvgl_ready = true;
    ESP_LOGI(TAG, "LVGL display initialized %ux%u", info.width, info.height);
    return true;
}

static bool shell_create_ui(void)
{
    ui_shell_layout_t layout;

    if (!ui_shell_get_layout(&layout)) {
        ESP_LOGE(TAG, "ui_shell_get_layout failed");
        return false;
    }
    if (!shell_lvgl_lock(portMAX_DELAY)) {
        ESP_LOGE(TAG, "LVGL lock failed during shell_create_ui");
        return false;
    }

    s_screen = lv_obj_create(NULL);
    if (s_screen == NULL) {
        shell_lvgl_unlock();
        ESP_LOGE(TAG, "screen create failed");
        return false;
    }
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_disp_load_scr(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_outline_width(s_screen, 0, 0);
    lv_obj_set_style_shadow_width(s_screen, 0, 0);
    lv_obj_set_style_radius(s_screen, 0, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);

    if (!orb_widget_bind_lvgl(s_screen,
                              layout.primary_visual_rect.x,
                              layout.primary_visual_rect.y,
                              layout.primary_visual_rect.width,
                              layout.primary_visual_rect.height)) {
        shell_lvgl_unlock();
        ESP_LOGE(TAG, "orb_widget_bind_lvgl failed");
        return false;
    }

    s_state_label = lv_label_create(s_screen);
    lv_label_set_text(s_state_label,
                      s_shell.status.presentation.status_text != NULL
                          ? s_shell.status.presentation.status_text
                          : "");
    lv_obj_set_style_text_color(s_state_label, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_text_font(s_state_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(s_state_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_state_label, 0, 0);
    lv_obj_align(s_state_label, LV_ALIGN_BOTTOM_MID, 0, -24);
    s_runtime_label = lv_label_create(s_screen);
    lv_label_set_text(s_runtime_label,
                      s_shell.status.runtime_status_detail[0] != '\0'
                          ? s_shell.status.runtime_status_detail
                          : "");
    lv_obj_set_style_text_color(s_runtime_label, lv_color_hex(0x94a3b8), 0);
    lv_obj_set_style_text_font(s_runtime_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_align(s_runtime_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_opa(s_runtime_label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_runtime_label, 0, 0);
    lv_obj_align(s_runtime_label, LV_ALIGN_BOTTOM_MID, 0, -6);

    s_drawer = NULL;
    s_drawer_pin = NULL;
    s_drawer_ssid = NULL;
    s_drawer_ip = NULL;
    s_drawer_volume = NULL;
    s_drawer_power = NULL;
    s_drawer_volume_slider = NULL;
    s_notification_drawer = NULL;
    s_notification_title = NULL;
    s_notification_hint = NULL;
    s_notification_empty = NULL;
    memset(s_notification_rows, 0, sizeof(s_notification_rows));
    memset(s_notification_row_labels, 0, sizeof(s_notification_row_labels));
    if (!shell_create_drawer_ui(&layout)) {
        shell_lvgl_unlock();
        ESP_LOGE(TAG, "drawer create failed");
        return false;
    }
    if (!shell_create_notification_drawer_ui(&layout)) {
        shell_lvgl_unlock();
        ESP_LOGE(TAG, "notification drawer create failed");
        return false;
    }

    shell_lvgl_unlock();
    if (shell_lvgl_lock(portMAX_DELAY)) {
        lv_obj_invalidate(s_screen);
        lv_refr_now(NULL);
        shell_lvgl_unlock();
    }
    shell_refresh_labels();
    shell_apply_drawer_visibility();
    ESP_LOGI(TAG, "UI shell created");
    return true;
}

static void shell_apply_drawer_visibility(void)
{
    ui_shell_layout_t layout;
    lv_anim_t anim;
    int32_t target_offset;
    int32_t target_notification_offset;

    if (s_drawer == NULL || s_notification_drawer == NULL || !ui_shell_get_layout(&layout)) {
        return;
    }

    if (!shell_lvgl_lock(portMAX_DELAY)) {
        return;
    }

    target_offset = s_shell.status.drawer_visible ? (int32_t)layout.drawer_rect.height : 0;
    lv_anim_del(s_drawer, shell_drawer_set_offset);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_drawer);
    lv_anim_set_exec_cb(&anim, shell_drawer_set_offset);
    lv_anim_set_values(&anim, s_drawer_offset, target_offset);
    lv_anim_set_time(&anim, s_shell.status.drawer_visible ? 280U : 220U);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);

    target_notification_offset = s_shell.status.notification_drawer_visible ? layout.screen_height : 0;
    lv_anim_del(s_notification_drawer, shell_notification_drawer_set_offset);
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, s_notification_drawer);
    lv_anim_set_exec_cb(&anim, shell_notification_drawer_set_offset);
    lv_anim_set_values(&anim, s_notification_drawer_offset, target_notification_offset);
    lv_anim_set_time(&anim, s_shell.status.notification_drawer_visible ? 260U : 220U);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_start(&anim);
    shell_lvgl_unlock();
}

static void shell_refresh_labels(void)
{
    drawer_snapshot_t snapshot;
    notification_tray_snapshot_t notification_snapshot;
    ui_shell_layout_t layout;
    char line[160];
    bool has_notifications = false;

    if (!ui_shell_get_layout(&layout)) {
        return;
    }
    if (!shell_lvgl_lock(portMAX_DELAY)) {
        return;
    }

    if (notification_tray_get_snapshot(&notification_snapshot) && notification_snapshot.count > 0U) {
        has_notifications = true;
        s_shell.status.notification_count = (uint8_t)notification_snapshot.count;
        strncpy(s_shell.status.notification_summary,
                notification_snapshot.items[0].summary,
                sizeof(s_shell.status.notification_summary) - 1);
        s_shell.status.notification_summary[sizeof(s_shell.status.notification_summary) - 1] = '\0';
    } else {
        s_shell.status.notification_count = 0;
        s_shell.status.notification_summary[0] = '\0';
    }

    if (s_state_label != NULL) {
        if (has_notifications && s_shell.status.app_state == APP_STATE_IDLE) {
            if (notification_snapshot.count == 1U) {
                shell_set_label_text(s_state_label, "1 CLAW V-MAIL ^");
            } else {
                snprintf(line, sizeof(line), "%u CLAW V-MAIL ^", (unsigned)notification_snapshot.count);
                shell_set_label_text(s_state_label, line);
            }
            lv_obj_set_style_text_color(s_state_label, lv_color_hex(SHELL_STATUS_NOTIFICATION_COLOR), 0);
        } else {
            shell_set_label_text(s_state_label,
                                 s_shell.status.presentation.status_text != NULL
                                     ? s_shell.status.presentation.status_text
                                     : "");
            lv_obj_set_style_text_color(s_state_label, lv_color_hex(SHELL_STATUS_DEFAULT_COLOR), 0);
        }
    }
    if (s_runtime_label != NULL) {
        shell_set_label_text(s_runtime_label, "");
        lv_obj_set_style_text_color(s_runtime_label, lv_color_hex(SHELL_RUNTIME_DEFAULT_COLOR), 0);
    }

    if (!drawer_get_snapshot(&snapshot)) {
        shell_lvgl_unlock();
        return;
    }

    if (s_drawer_pin != NULL) {
        shell_set_label_text(s_drawer_pin, snapshot.pin);
    }
    if (s_drawer_ssid != NULL) {
        shell_set_label_text(s_drawer_ssid, snapshot.ssid);
    }
    if (s_drawer_ip != NULL) {
        shell_set_label_text(s_drawer_ip, snapshot.ip);
    }
    if (s_drawer_volume_slider != NULL) {
        lv_slider_set_value(s_drawer_volume_slider, snapshot.volume, LV_ANIM_OFF);
    }
    if (s_drawer_power != NULL) {
        if (snapshot.battery_present) {
            snprintf(line, sizeof(line), "Power: %d%%  USB %s", snapshot.battery_percent, snapshot.usb_connected ? "on" : "off");
        } else {
            snprintf(line, sizeof(line), "Power: USB %s", snapshot.usb_connected ? "on" : "off");
        }
        shell_set_label_text(s_drawer_power, line);
    }

    if (s_notification_empty != NULL) {
        if (has_notifications) {
            lv_obj_add_flag(s_notification_empty, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_notification_empty, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (has_notifications) {
        size_t index;
        for (index = 0; index < NOTIFICATION_TRAY_MAX_ITEMS; ++index) {
            if (index < notification_snapshot.count &&
                index < SHELL_NOTIFICATION_VISIBLE_ROWS &&
                notification_snapshot.items[index].occupied) {
                if (s_notification_row_labels[index] != NULL) {
                    shell_set_label_text(s_notification_row_labels[index], notification_snapshot.items[index].summary);
                }
                if (s_notification_rows[index] != NULL) {
                    lv_obj_clear_flag(s_notification_rows[index], LV_OBJ_FLAG_HIDDEN);
                    s_notification_row_hitboxes[index].x1 = 32;
                    s_notification_row_hitboxes[index].x2 = (int16_t)(layout.screen_width - 32);
                    s_notification_row_hitboxes[index].y1 = (int16_t)(84 + (index * 56));
                    s_notification_row_hitboxes[index].y2 = (int16_t)(s_notification_row_hitboxes[index].y1 + 48);
                }
            } else if (s_notification_rows[index] != NULL) {
                lv_obj_add_flag(s_notification_rows[index], LV_OBJ_FLAG_HIDDEN);
                memset(&s_notification_row_hitboxes[index], 0, sizeof(s_notification_row_hitboxes[index]));
            }
        }
    } else {
        size_t index;
        for (index = 0; index < NOTIFICATION_TRAY_MAX_ITEMS; ++index) {
            if (s_notification_rows[index] != NULL) {
                lv_obj_add_flag(s_notification_rows[index], LV_OBJ_FLAG_HIDDEN);
            }
            memset(&s_notification_row_hitboxes[index], 0, sizeof(s_notification_row_hitboxes[index]));
        }
    }
    shell_lvgl_unlock();
}

static ui_shell_layout_t shell_build_layout(const board_profile_t *board)
{
    const layout_profile_t *layout = &board->layout;
    ui_shell_layout_t out;
    int16_t primary_x = 0;
    int16_t primary_y = 0;
    uint16_t primary_width = layout->screen_width;

    memset(&out, 0, sizeof(out));

    if (layout->display_shape == DISPLAY_SHAPE_ROUND) {
        primary_width = layout->primary_visual_height;
        if (primary_width > layout->screen_width) {
            primary_width = layout->screen_width;
        }
        primary_x = (int16_t)((layout->screen_width - primary_width) / 2);
        primary_y = (int16_t)((layout->screen_height - layout->primary_visual_height) / 2);
        if (primary_y < 0) {
            primary_y = 0;
        }
    } else if (layout->primary_visual_align == VISUAL_ALIGN_CENTER) {
        primary_x = (int16_t)((layout->screen_width - primary_width) / 2);
        primary_y = (int16_t)((layout->screen_height - layout->primary_visual_height) / 2);
    } else {
        primary_y = (int16_t)layout->primary_visual_top_offset;
    }

    out.display_shape = layout->display_shape;
    out.screen_width = layout->screen_width;
    out.screen_height = layout->screen_height;
    out.primary_visual_rect.x = primary_x;
    out.primary_visual_rect.y = primary_y;
    out.primary_visual_rect.width = primary_width;
    out.primary_visual_rect.height = layout->primary_visual_height;
    out.state_label_center_x = (int16_t)(layout->screen_width / 2);
    out.state_label_baseline_y = (int16_t)(layout->screen_height - layout->state_label_bottom_offset);
    out.drawer_rect.x = 0;
    out.drawer_rect.y = 0;
    out.drawer_rect.width = layout->screen_width;
    out.drawer_rect.height = layout->drawer_height;
    out.drawer_hidden_y = -(int16_t)layout->drawer_height;
    out.drawer_open_y = 0;
    out.drawer_trigger_zone = layout->drawer_trigger_zone;
    return out;
}

static void shell_on_volume_changed(int volume, void *ctx)
{
    (void)ctx;

    settings_service_set_volume((uint8_t)volume);
    if (s_shell.config.on_volume_changed != NULL) {
        s_shell.config.on_volume_changed(volume, s_shell.config.ctx);
    }
}

bool ui_shell_init(const board_profile_t *board, const ui_shell_config_t *config)
{
    drawer_config_t drawer_config;
    uint8_t initial_volume = 70;

    if (board == NULL) {
        return false;
    }

    memset(&s_shell, 0, sizeof(s_shell));
    s_shell.board = *board;
    if (config != NULL) {
        s_shell.config = *config;
    }

    s_shell.status.app_state = APP_STATE_BOOT;
    s_shell.status.presentation = presentation_from_state(APP_STATE_BOOT);
    s_shell.status.locked = false;
    s_shell.status.drawer_visible = false;
    s_shell.status.notification_drawer_visible = false;
    s_shell.status.runtime_available = false;
    s_shell.status.runtime_status = RUNTIME_STATUS_DISCONNECTED;
    s_shell.status.notification_count = 0;
    s_shell.status.notification_summary[0] = '\0';
    s_shell.status.runtime_status_detail[0] = '\0';

    memset(&drawer_config, 0, sizeof(drawer_config));
    drawer_config.on_volume_changed = shell_on_volume_changed;
    drawer_config.ctx = NULL;
    if (!settings_service_get_volume(&initial_volume)) {
        initial_volume = 70;
    }
    drawer_config.initial_volume = (int)initial_volume;

    if (!drawer_init(&drawer_config)) {
        ESP_LOGE(TAG, "drawer_init failed");
        return false;
    }
    if (!shell_lvgl_init_display()) {
        ESP_LOGE(TAG, "shell_lvgl_init_display failed");
        return false;
    }
    if (!orb_widget_init()) {
        ESP_LOGE(TAG, "orb_widget_init failed");
        return false;
    }
    if (!notification_tray_init()) {
        ESP_LOGE(TAG, "notification_tray_init failed");
        return false;
    }

    s_shell.initialized = true;
    ui_shell_sync_from_services();
    orb_widget_apply_presentation(s_shell.status.presentation.visual);
    if (!shell_create_ui()) {
        ESP_LOGE(TAG, "shell_create_ui failed");
        return false;
    }
    ESP_LOGI(TAG, "UI shell initialized");
    return true;
}

void ui_shell_apply_presentation(presentation_state_t presentation)
{
    s_shell.status.presentation = presentation;
    orb_widget_apply_presentation(presentation.visual);
    shell_refresh_labels();
}

void ui_shell_set_locked(bool locked)
{
    s_shell.status.locked = locked;

    if (locked) {
        s_shell.status.app_state = APP_STATE_LOCKED;
        s_shell.status.presentation = presentation_from_state(APP_STATE_LOCKED);
        drawer_close();
        s_shell.status.notification_drawer_visible = false;
    } else {
        s_shell.status.app_state = APP_STATE_IDLE;
        s_shell.status.presentation = presentation_from_state(APP_STATE_IDLE);
    }

    s_shell.status.drawer_visible = drawer_is_visible();
    shell_refresh_labels();
    shell_apply_drawer_visibility();
}

bool ui_shell_handle_input_event(const input_event_t *event)
{
    if (!s_shell.initialized || event == NULL) {
        return false;
    }

    switch (event->type) {
    case INPUT_EVENT_PRESS:
        if (drawer_is_visible() &&
            event->has_coordinates &&
            shell_point_in_area(event->x, event->y, &s_drawer_volume_slider_hitbox)) {
            int volume = shell_slider_value_from_x(event->x, &s_drawer_volume_slider_hitbox);

            drawer_set_volume(volume);
            s_drawer_volume_interaction = true;
            s_shell.status.drawer_visible = drawer_is_visible();
            shell_refresh_labels();
            shell_apply_drawer_visibility();
            return true;
        }
        s_drawer_volume_interaction = false;
        break;
    case INPUT_EVENT_GESTURE_SWIPE_DOWN:
        if (s_shell.status.notification_drawer_visible) {
            s_shell.status.notification_drawer_visible = false;
        } else {
            drawer_open();
        }
        break;
    case INPUT_EVENT_GESTURE_SWIPE_UP:
        if (drawer_is_visible()) {
            drawer_close();
        } else if (s_shell.status.notification_count > 0U) {
            s_shell.status.notification_drawer_visible = true;
        }
        break;
    case INPUT_EVENT_TAP:
        if (s_drawer_volume_interaction) {
            s_drawer_volume_interaction = false;
            break;
        }
        if (s_shell.status.notification_drawer_visible && event->has_coordinates) {
            notification_tray_snapshot_t notification_snapshot;
            ui_shell_layout_t layout;
            size_t index;
            bool selected = false;

            if (ui_shell_get_layout(&layout) && notification_tray_get_snapshot(&notification_snapshot)) {
                for (index = 0; index < notification_snapshot.count && index < NOTIFICATION_TRAY_MAX_ITEMS; ++index) {
                    if (notification_snapshot.items[index].occupied &&
                        shell_point_in_area(event->x, event->y, &s_notification_row_hitboxes[index])) {
                        selected = true;
                        break;
                    }
                }
                if (!selected &&
                    shell_notification_index_from_point(&layout,
                                                        event->x,
                                                        event->y,
                                                        notification_snapshot.count,
                                                        &index) &&
                    notification_snapshot.items[index].occupied) {
                    selected = true;
                }
                if (selected) {
                    ESP_LOGI(TAG,
                             "Notification selected idx=%u id=%s x=%d y=%d",
                             (unsigned)index,
                             notification_snapshot.items[index].id,
                             (int)event->x,
                             (int)event->y);
                    s_shell.status.notification_drawer_visible = false;
                    if (s_shell.config.on_notification_selected != NULL) {
                        s_shell.config.on_notification_selected(notification_snapshot.items[index].id, s_shell.config.ctx);
                    }
                    s_shell.status.drawer_visible = drawer_is_visible();
                    shell_refresh_labels();
                    shell_apply_drawer_visibility();
                    return true;
                }
            }
            s_shell.status.notification_drawer_visible = false;
            break;
        }
        if (drawer_is_visible()) {
            drawer_close();
            break;
        }
        if (s_shell.status.locked) {
            break;
        }
        if (s_shell.status.app_state == APP_STATE_IDLE) {
            if (s_shell.config.on_primary_tap_when_idle != NULL) {
                s_shell.config.on_primary_tap_when_idle(s_shell.config.ctx);
            }
            break;
        }
        if (s_shell.status.app_state == APP_STATE_LISTENING) {
            if (s_shell.config.on_primary_tap_when_listening != NULL) {
                s_shell.config.on_primary_tap_when_listening(s_shell.config.ctx);
            }
            break;
        }
        if (s_shell.status.app_state == APP_STATE_ERROR) {
            if (s_shell.config.on_error_tap != NULL) {
                s_shell.config.on_error_tap(s_shell.config.ctx);
            }
            break;
        }
        break;
    default:
        break;
    }

    s_shell.status.drawer_visible = drawer_is_visible();
    shell_apply_drawer_visibility();
    return true;
}

void ui_shell_set_drawer_data(const char *pin, const char *ip, const char *ssid, int volume, int battery_percent)
{
    drawer_update(pin, ip, ssid, volume, battery_percent);
    s_shell.status.drawer_visible = drawer_is_visible();
    shell_refresh_labels();
}

void ui_shell_set_power_status(bool battery_present, int battery_percent, bool usb_connected)
{
    drawer_set_power_status(battery_present, battery_percent, usb_connected);
    shell_refresh_labels();
}

bool ui_shell_get_drawer_snapshot(drawer_snapshot_t *out_snapshot)
{
    return drawer_get_snapshot(out_snapshot);
}

bool ui_shell_get_status(ui_shell_status_t *out_status)
{
    if (out_status == NULL) {
        return false;
    }

    *out_status = s_shell.status;
    return true;
}

bool ui_shell_get_layout(ui_shell_layout_t *out_layout)
{
    if (out_layout == NULL || !s_shell.initialized) {
        return false;
    }

    *out_layout = shell_build_layout(&s_shell.board);
    return true;
}

void ui_shell_sync_from_state(app_state_t state)
{
    if (!shell_lvgl_lock(portMAX_DELAY)) {
        return;
    }
    s_shell.status.app_state = state;
    s_shell.status.presentation = presentation_from_state(state);
    orb_widget_apply_presentation(s_shell.status.presentation.visual);
    shell_refresh_labels();
    shell_lvgl_unlock();
}

bool ui_shell_notification_upsert(const char *notification_id, const char *summary)
{
    bool updated = notification_tray_upsert(notification_id, summary, NULL);

    if (updated) {
        ESP_LOGI(TAG, "notification upsert id=%s summary=%s", notification_id, summary);
        shell_refresh_labels();
        shell_apply_drawer_visibility();
    } else {
        ESP_LOGW(TAG, "notification upsert failed id=%s", notification_id != NULL ? notification_id : "");
    }

    return updated;
}

bool ui_shell_notification_remove(const char *notification_id)
{
    bool removed = notification_tray_remove(notification_id);

    if (removed) {
        ESP_LOGI(TAG, "notification remove id=%s", notification_id);
        if (s_shell.status.notification_count <= 1U) {
            s_shell.status.notification_drawer_visible = false;
        }
        shell_refresh_labels();
        shell_apply_drawer_visibility();
    } else {
        ESP_LOGW(TAG, "notification remove missed id=%s", notification_id != NULL ? notification_id : "");
    }

    return removed;
}

void ui_shell_notification_clear_all(void)
{
    notification_tray_clear();
    s_shell.status.notification_drawer_visible = false;
    shell_refresh_labels();
    shell_apply_drawer_visibility();
}

void ui_shell_sync_from_services(void)
{
    char pin[8];
    char ip[64];
    char ssid[64];
    uint8_t volume = 70;
    power_status_t power;
    runtime_snapshot_t runtime;
    bool have_power = false;
    orb_config_t orb_config;

    if (!shell_lvgl_lock(portMAX_DELAY)) {
        return;
    }

    memset(pin, 0, sizeof(pin));
    memset(ip, 0, sizeof(ip));
    memset(ssid, 0, sizeof(ssid));
    memset(&power, 0, sizeof(power));
    memset(&runtime, 0, sizeof(runtime));
    memset(&orb_config, 0, sizeof(orb_config));

    if (!security_auth_get_pin(pin, sizeof(pin))) {
        strncpy(pin, "------", sizeof(pin) - 1);
    }
    if (!wifi_setup_service_get_saved_ssid(ssid, sizeof(ssid))) {
        strncpy(ssid, "---", sizeof(ssid) - 1);
    }
    if (!network_platform_get_ip(ip, sizeof(ip))) {
        strncpy(ip, "---", sizeof(ip) - 1);
    }
    settings_service_get_volume(&volume);
    have_power = power_platform_get_status(&power);
    s_shell.status.runtime_available = runtime_control_is_available();
    if (runtime_control_get_snapshot(&runtime)) {
        s_shell.status.runtime_status = runtime.status;
        strncpy(s_shell.status.runtime_status_detail,
                runtime.status_detail,
                sizeof(s_shell.status.runtime_status_detail) - 1);
        s_shell.status.runtime_status_detail[sizeof(s_shell.status.runtime_status_detail) - 1] = '\0';
    } else {
        s_shell.status.runtime_status = RUNTIME_STATUS_DISCONNECTED;
        s_shell.status.runtime_status_detail[0] = '\0';
    }

    drawer_update(pin, ip, ssid, (int)volume, have_power ? power.battery_percent : -1);
    drawer_set_power_status(have_power ? power.battery_present : false,
                            have_power ? power.battery_percent : -1,
                            have_power ? power.usb_connected : false);
    if (orb_service_get_config(&orb_config)) {
        orb_widget_apply_config(&orb_config);
    }
    s_shell.status.drawer_visible = drawer_is_visible();
    shell_refresh_labels();
    shell_apply_drawer_visibility();

    shell_lvgl_unlock();
}

void ui_shell_process(void)
{
    if (!s_lvgl_ready) {
        return;
    }

    if (shell_lvgl_lock(pdMS_TO_TICKS(50))) {
        lv_timer_handler();
        shell_lvgl_unlock();
    }
}
