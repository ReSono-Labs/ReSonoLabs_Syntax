#include "notification_tray.h"

#include <string.h>

static notification_tray_snapshot_t s_snapshot;

static void copy_text(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0U) {
        return;
    }

    if (src == NULL) {
        src = "";
    }

    strncpy(dst, src, dst_len - 1U);
    dst[dst_len - 1U] = '\0';
}

static int notification_tray_find_index(const char *id)
{
    size_t index;

    if (id == NULL || id[0] == '\0') {
        return -1;
    }

    for (index = 0; index < NOTIFICATION_TRAY_MAX_ITEMS; ++index) {
        if (s_snapshot.items[index].occupied && strcmp(s_snapshot.items[index].id, id) == 0) {
            return (int)index;
        }
    }

    return -1;
}

static int notification_tray_find_empty_slot(void)
{
    size_t index;

    for (index = 0; index < NOTIFICATION_TRAY_MAX_ITEMS; ++index) {
        if (!s_snapshot.items[index].occupied) {
            return (int)index;
        }
    }

    return -1;
}

static void notification_tray_compact(void)
{
    size_t read_index;
    size_t write_index = 0;

    for (read_index = 0; read_index < NOTIFICATION_TRAY_MAX_ITEMS; ++read_index) {
        if (!s_snapshot.items[read_index].occupied) {
            continue;
        }
        if (write_index != read_index) {
            s_snapshot.items[write_index] = s_snapshot.items[read_index];
            memset(&s_snapshot.items[read_index], 0, sizeof(s_snapshot.items[read_index]));
        }
        ++write_index;
    }

    while (write_index < NOTIFICATION_TRAY_MAX_ITEMS) {
        memset(&s_snapshot.items[write_index], 0, sizeof(s_snapshot.items[write_index]));
        ++write_index;
    }

    s_snapshot.count = 0;
    for (read_index = 0; read_index < NOTIFICATION_TRAY_MAX_ITEMS; ++read_index) {
        if (s_snapshot.items[read_index].occupied) {
            ++s_snapshot.count;
        }
    }
}

bool notification_tray_init(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    return true;
}

bool notification_tray_upsert(const char *id, const char *summary, const char *detail)
{
    int index;

    if (id == NULL || id[0] == '\0' || summary == NULL || summary[0] == '\0') {
        return false;
    }

    index = notification_tray_find_index(id);
    if (index < 0) {
        index = notification_tray_find_empty_slot();
    }
    if (index < 0) {
        return false;
    }

    s_snapshot.items[index].occupied = true;
    copy_text(s_snapshot.items[index].id, sizeof(s_snapshot.items[index].id), id);
    copy_text(s_snapshot.items[index].summary, sizeof(s_snapshot.items[index].summary), summary);
    copy_text(s_snapshot.items[index].detail, sizeof(s_snapshot.items[index].detail), detail);
    notification_tray_compact();
    return true;
}

bool notification_tray_remove(const char *id)
{
    int index = notification_tray_find_index(id);

    if (index < 0) {
        return false;
    }

    memset(&s_snapshot.items[index], 0, sizeof(s_snapshot.items[index]));
    notification_tray_compact();
    return true;
}

void notification_tray_clear(void)
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
}

bool notification_tray_get_snapshot(notification_tray_snapshot_t *out_snapshot)
{
    if (out_snapshot == NULL) {
        return false;
    }

    *out_snapshot = s_snapshot;
    return true;
}

bool notification_tray_get_item(const char *id, notification_tray_item_t *out_item)
{
    int index;

    if (id == NULL || id[0] == '\0' || out_item == NULL) {
        return false;
    }

    index = notification_tray_find_index(id);
    if (index < 0) {
        return false;
    }

    *out_item = s_snapshot.items[index];
    return true;
}
