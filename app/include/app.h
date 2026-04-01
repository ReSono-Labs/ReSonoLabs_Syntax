#ifndef APP_H
#define APP_H

#include <stdbool.h>

#include "board_profile.h"

bool app_bootstrap(const board_profile_t *board);
bool app_bootstrap_by_id(const char *board_id);
const board_profile_t *app_get_active_board(void);
void app_run(void);

#endif
