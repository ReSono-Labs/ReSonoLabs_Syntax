#include "provider_storage.h"

#include <string.h>

#include "nvs.h"
#include "storage_platform.h"

#define PROVIDER_RUNTIME_NAMESPACE "provider_rt"
#define PROVIDER_KEY_GATEWAY_TOKEN "gateway_token"
#define PROVIDER_KEY_ENDPOINT_HOST "endpoint_host"
#define PROVIDER_KEY_ENDPOINT_PORT "endpoint_port"
#define PROVIDER_KEY_DEVICE_TOKEN "device_token"
#define PROVIDER_KEY_DEVICE_SEED "device_seed"

static bool erase_key(const char *key)
{
    nvs_handle_t handle;
    esp_err_t err;

    if (key == 0 || !storage_platform_init()) {
        return false;
    }
    if (nvs_open(PROVIDER_RUNTIME_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) {
        return true;
    }

    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err == ESP_OK;
}

bool provider_storage_load_gateway_token(char *buf, size_t buf_len)
{
    return storage_platform_get_str(PROVIDER_RUNTIME_NAMESPACE, PROVIDER_KEY_GATEWAY_TOKEN, buf, buf_len);
}

bool provider_storage_save_gateway_token(const char *token)
{
    return storage_platform_set_str(PROVIDER_RUNTIME_NAMESPACE, PROVIDER_KEY_GATEWAY_TOKEN, token ? token : "");
}

bool provider_storage_clear_gateway_token(void)
{
    return erase_key(PROVIDER_KEY_GATEWAY_TOKEN);
}

bool provider_storage_load_endpoint_host(char *buf, size_t buf_len)
{
    return storage_platform_get_str(PROVIDER_RUNTIME_NAMESPACE, PROVIDER_KEY_ENDPOINT_HOST, buf, buf_len);
}

bool provider_storage_save_endpoint_host(const char *host)
{
    return storage_platform_set_str(PROVIDER_RUNTIME_NAMESPACE, PROVIDER_KEY_ENDPOINT_HOST, host ? host : "");
}

bool provider_storage_clear_endpoint_host(void)
{
    return erase_key(PROVIDER_KEY_ENDPOINT_HOST);
}

bool provider_storage_load_endpoint_port(uint16_t *port)
{
    int32_t value = 0;

    if (port == NULL) {
        return false;
    }
    if (!storage_platform_get_i32(PROVIDER_RUNTIME_NAMESPACE, PROVIDER_KEY_ENDPOINT_PORT, &value) ||
        value <= 0 || value > 65535) {
        return false;
    }

    *port = (uint16_t)value;
    return true;
}

bool provider_storage_save_endpoint_port(uint16_t port)
{
    if (port == 0) {
        return false;
    }
    return storage_platform_set_i32(PROVIDER_RUNTIME_NAMESPACE, PROVIDER_KEY_ENDPOINT_PORT, (int32_t)port);
}

bool provider_storage_clear_endpoint_port(void)
{
    return erase_key(PROVIDER_KEY_ENDPOINT_PORT);
}

bool provider_storage_load_device_token(char *buf, size_t buf_len)
{
    return storage_platform_get_str(PROVIDER_RUNTIME_NAMESPACE, PROVIDER_KEY_DEVICE_TOKEN, buf, buf_len);
}

bool provider_storage_save_device_token(const char *token)
{
    return storage_platform_set_str(PROVIDER_RUNTIME_NAMESPACE, PROVIDER_KEY_DEVICE_TOKEN, token ? token : "");
}

bool provider_storage_clear_device_token(void)
{
    return erase_key(PROVIDER_KEY_DEVICE_TOKEN);
}

bool provider_storage_load_device_seed(uint8_t *buf, size_t *buf_len)
{
    return storage_platform_get_blob(PROVIDER_RUNTIME_NAMESPACE, PROVIDER_KEY_DEVICE_SEED, buf, buf_len);
}

bool provider_storage_save_device_seed(const uint8_t *seed, size_t seed_len)
{
    return storage_platform_set_blob(PROVIDER_RUNTIME_NAMESPACE, PROVIDER_KEY_DEVICE_SEED, seed, seed_len);
}

bool provider_storage_clear_device_seed(void)
{
    return erase_key(PROVIDER_KEY_DEVICE_SEED);
}
