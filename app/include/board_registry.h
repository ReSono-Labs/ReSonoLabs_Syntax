#ifndef BOARD_REGISTRY_H
#define BOARD_REGISTRY_H

#include <stddef.h>

#include "board_profile.h"

const board_profile_t *board_registry_get_default(void);
const board_profile_t *board_registry_find(const char *board_id);
size_t board_registry_count(void);
const board_profile_t *board_registry_get_at(size_t index);

#endif
