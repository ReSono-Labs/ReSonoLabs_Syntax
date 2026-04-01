#ifndef NOTIFICATION_TRAY_H
#define NOTIFICATION_TRAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NOTIFICATION_TRAY_MAX_ITEMS 6
#define NOTIFICATION_TRAY_ID_MAX_LEN 64
#define NOTIFICATION_TRAY_SUMMARY_MAX_LEN 96
#define NOTIFICATION_TRAY_DETAIL_MAX_LEN 640

typedef struct {
    char id[NOTIFICATION_TRAY_ID_MAX_LEN];
    char summary[NOTIFICATION_TRAY_SUMMARY_MAX_LEN];
    char detail[NOTIFICATION_TRAY_DETAIL_MAX_LEN];
    bool occupied;
} notification_tray_item_t;

typedef struct {
    notification_tray_item_t items[NOTIFICATION_TRAY_MAX_ITEMS];
    size_t count;
} notification_tray_snapshot_t;

bool notification_tray_init(void);
bool notification_tray_upsert(const char *id, const char *summary, const char *detail);
bool notification_tray_remove(const char *id);
void notification_tray_clear(void);
bool notification_tray_get_snapshot(notification_tray_snapshot_t *out_snapshot);
bool notification_tray_get_item(const char *id, notification_tray_item_t *out_item);

#endif
