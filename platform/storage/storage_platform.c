#include "storage_platform.h"

#include "nvs.h"
#include "nvs_flash.h"

static bool s_initialized;

static bool open_namespace_rw(const char *ns, nvs_handle_t *out_handle)
{
    if (ns == 0 || out_handle == 0) {
        return false;
    }
    return nvs_open(ns, NVS_READWRITE, out_handle) == ESP_OK;
}

static bool open_namespace_ro(const char *ns, nvs_handle_t *out_handle)
{
    if (ns == 0 || out_handle == 0) {
        return false;
    }
    return nvs_open(ns, NVS_READONLY, out_handle) == ESP_OK;
}

bool storage_platform_init(void)
{
    esp_err_t err;

    if (s_initialized) {
        return true;
    }

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() != ESP_OK) {
            return false;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        return false;
    }

    s_initialized = true;
    return true;
}

bool storage_platform_get_i32(const char *ns, const char *key, int32_t *value)
{
    nvs_handle_t handle;

    if (key == 0 || value == 0 || !storage_platform_init() || !open_namespace_ro(ns, &handle)) {
        return false;
    }

    if (nvs_get_i32(handle, key, value) != ESP_OK) {
        nvs_close(handle);
        return false;
    }

    nvs_close(handle);
    return true;
}

bool storage_platform_set_i32(const char *ns, const char *key, int32_t value)
{
    nvs_handle_t handle;
    esp_err_t err;

    if (key == 0 || !storage_platform_init() || !open_namespace_rw(ns, &handle)) {
        return false;
    }

    err = nvs_set_i32(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err == ESP_OK;
}

bool storage_platform_get_str(const char *ns, const char *key, char *buf, size_t buf_len)
{
    nvs_handle_t handle;
    size_t len = buf_len;

    if (buf != 0 && buf_len > 0) {
        buf[0] = '\0';
    }
    if (key == 0 || buf == 0 || buf_len == 0 || !storage_platform_init() || !open_namespace_ro(ns, &handle)) {
        return false;
    }

    if (nvs_get_str(handle, key, buf, &len) != ESP_OK) {
        nvs_close(handle);
        buf[0] = '\0';
        return false;
    }

    nvs_close(handle);
    return true;
}

bool storage_platform_set_str(const char *ns, const char *key, const char *value)
{
    nvs_handle_t handle;
    esp_err_t err;

    if (key == 0 || !storage_platform_init() || !open_namespace_rw(ns, &handle)) {
        return false;
    }

    err = nvs_set_str(handle, key, value ? value : "");
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err == ESP_OK;
}

bool storage_platform_get_blob(const char *ns, const char *key, void *buf, size_t *buf_len)
{
    nvs_handle_t handle;

    if (key == 0 || buf == 0 || buf_len == 0 || *buf_len == 0 ||
        !storage_platform_init() || !open_namespace_ro(ns, &handle)) {
        return false;
    }

    if (nvs_get_blob(handle, key, buf, buf_len) != ESP_OK) {
        nvs_close(handle);
        return false;
    }

    nvs_close(handle);
    return true;
}

bool storage_platform_set_blob(const char *ns, const char *key, const void *value, size_t value_len)
{
    nvs_handle_t handle;
    esp_err_t err;

    if (key == 0 || value == 0 || value_len == 0 ||
        !storage_platform_init() || !open_namespace_rw(ns, &handle)) {
        return false;
    }

    err = nvs_set_blob(handle, key, value, value_len);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err == ESP_OK;
}

bool storage_platform_erase_all(void)
{
    esp_err_t err;

    s_initialized = false;
    err = nvs_flash_erase();
    if (err != ESP_OK) {
        return false;
    }

    return storage_platform_init();
}
