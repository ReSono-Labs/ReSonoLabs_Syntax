#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool ota_service_init(void);
bool ota_service_begin(size_t content_length);
bool ota_service_write_chunk(const uint8_t *data, size_t len);
bool ota_service_finish(void);

#endif
