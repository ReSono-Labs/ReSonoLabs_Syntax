#include "ota_service.h"

#include "esp_ota_ops.h"

static esp_ota_handle_t s_ota_handle;
static const esp_partition_t *s_update_partition;
static bool s_ota_active;

static void ota_service_reset_state(void)
{
    s_ota_handle = 0;
    s_update_partition = 0;
    s_ota_active = false;
}

bool ota_service_init(void)
{
    ota_service_reset_state();
    return true;
}

bool ota_service_begin(size_t content_length)
{
    (void)content_length;

    if (s_ota_active) {
        esp_ota_abort(s_ota_handle);
        ota_service_reset_state();
    }

    s_update_partition = esp_ota_get_next_update_partition(0);
    if (s_update_partition == 0) {
        return false;
    }
    if (esp_ota_begin(s_update_partition, OTA_WITH_SEQUENTIAL_WRITES, &s_ota_handle) != ESP_OK) {
        ota_service_reset_state();
        return false;
    }

    s_ota_active = true;
    return true;
}

bool ota_service_write_chunk(const uint8_t *data, size_t len)
{
    if (!s_ota_active || data == 0 || len == 0) {
        return false;
    }
    return esp_ota_write(s_ota_handle, data, len) == ESP_OK;
}

bool ota_service_finish(void)
{
    bool ok;

    if (!s_ota_active) {
        return false;
    }

    ok = esp_ota_end(s_ota_handle) == ESP_OK &&
         esp_ota_set_boot_partition(s_update_partition) == ESP_OK;
    if (!ok) {
        esp_ota_abort(s_ota_handle);
    }
    ota_service_reset_state();
    return ok;
}
