#ifndef STORAGE_PLATFORM_H
#define STORAGE_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool storage_platform_init(void);
bool storage_platform_get_i32(const char *ns, const char *key, int32_t *value);
bool storage_platform_set_i32(const char *ns, const char *key, int32_t value);
bool storage_platform_get_str(const char *ns, const char *key, char *buf, size_t buf_len);
bool storage_platform_set_str(const char *ns, const char *key, const char *value);
bool storage_platform_get_blob(const char *ns, const char *key, void *buf, size_t *buf_len);
bool storage_platform_set_blob(const char *ns, const char *key, const void *value, size_t value_len);
bool storage_platform_erase_all(void);

#endif
