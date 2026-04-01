#ifndef SETTINGS_SERVICE_H
#define SETTINGS_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool settings_service_init(void);
bool settings_service_get_volume(uint8_t *out_volume);
bool settings_service_set_volume(uint8_t volume);
bool settings_service_get_brightness(uint8_t *out_brightness);
bool settings_service_set_brightness(uint8_t brightness);

#endif
