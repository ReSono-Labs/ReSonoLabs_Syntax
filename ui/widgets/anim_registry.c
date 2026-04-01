#include "anim_registry.h"

#include <string.h>

extern const anim_descriptor_t g_anim_aurora;
extern const anim_descriptor_t g_anim_compass;
extern const anim_descriptor_t g_anim_helix;
extern const anim_descriptor_t g_anim_lattice;
extern const anim_descriptor_t g_anim_lissajous;
extern const anim_descriptor_t g_anim_nucleus;
extern const anim_descriptor_t g_anim_radar;
extern const anim_descriptor_t g_anim_vortex;

static const anim_descriptor_t *const s_registry[] = {
    &g_anim_nucleus,
    &g_anim_vortex,
    &g_anim_lissajous,
    &g_anim_lattice,
    &g_anim_compass,
    &g_anim_aurora,
    &g_anim_radar,
    &g_anim_helix,
};

int anim_registry_count(void)
{
    return (int)(sizeof(s_registry) / sizeof(s_registry[0]));
}

const anim_descriptor_t *anim_registry_get(int index)
{
    if (index < 0 || index >= anim_registry_count()) {
        return NULL;
    }

    return s_registry[index];
}

const anim_descriptor_t *anim_registry_find_by_id(uint8_t id)
{
    int count = anim_registry_count();

    for (int i = 0; i < count; ++i) {
        if (s_registry[i]->id == id) {
            return s_registry[i];
        }
    }

    return NULL;
}

const anim_descriptor_t *anim_registry_find_by_name(const char *name)
{
    int count = anim_registry_count();

    if (name == NULL) {
        return NULL;
    }

    for (int i = 0; i < count; ++i) {
        if (s_registry[i]->name != NULL && strcmp(s_registry[i]->name, name) == 0) {
            return s_registry[i];
        }
    }

    return NULL;
}
