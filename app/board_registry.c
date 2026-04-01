#include "board_registry.h"

#include <stddef.h>
#include <string.h>

#include "s3_1_85c_board.h"

static const board_profile_t *const s_known_boards[] = {
    &g_s3_1_85c_board,
};

const board_profile_t *board_registry_get_default(void)
{
    return &g_s3_1_85c_board;
}

const board_profile_t *board_registry_find(const char *board_id)
{
    size_t i;

    if (board_id == NULL || board_id[0] == '\0') {
        return NULL;
    }

    for (i = 0; i < (sizeof(s_known_boards) / sizeof(s_known_boards[0])); ++i) {
        const board_profile_t *board = s_known_boards[i];

        if (board != NULL && board->board_id != NULL && strcmp(board->board_id, board_id) == 0) {
            return board;
        }
    }

    return NULL;
}

size_t board_registry_count(void)
{
    return sizeof(s_known_boards) / sizeof(s_known_boards[0]);
}

const board_profile_t *board_registry_get_at(size_t index)
{
    if (index >= board_registry_count()) {
        return NULL;
    }

    return s_known_boards[index];
}
