#include "wifi_setup_service.h"

#include <string.h>
#include <stdlib.h>

#include "storage_platform.h"

#define WIFI_SETUP_NAMESPACE "wifi_setup"
#define KEY_SSID "ssid"
#define KEY_PASSWORD "password"

static bool maybe_url_encoded(const char *value)
{
    if (value == NULL) {
        return false;
    }

    for (size_t i = 0; value[i] != '\0'; ++i) {
        if (value[i] == '+' || (value[i] == '%' && value[i + 1] != '\0' && value[i + 2] != '\0')) {
            return true;
        }
    }

    return false;
}

static void url_decode_inplace(char *value, size_t value_len)
{
    size_t si = 0;
    size_t di = 0;

    if (value == NULL || value_len == 0) {
        return;
    }

    while (value[si] != '\0' && di + 1 < value_len) {
        if (value[si] == '%' && value[si + 1] != '\0' && value[si + 2] != '\0') {
            char hex[3] = {value[si + 1], value[si + 2], '\0'};

            value[di++] = (char)strtol(hex, NULL, 16);
            si += 3;
        } else if (value[si] == '+') {
            value[di++] = ' ';
            si++;
        } else {
            value[di++] = value[si++];
        }
    }

    value[di] = '\0';
}

static bool load_and_normalize_string(const char *key, char *buf, size_t buf_len)
{
    bool loaded;

    if (buf != 0 && buf_len > 0) {
        buf[0] = '\0';
    }
    if (key == 0 || buf == 0 || buf_len == 0 || !wifi_setup_service_init()) {
        return false;
    }

    loaded = storage_platform_get_str(WIFI_SETUP_NAMESPACE, key, buf, buf_len);
    if (!loaded || buf[0] == '\0') {
        return false;
    }

    if (maybe_url_encoded(buf)) {
        url_decode_inplace(buf, buf_len);
        storage_platform_set_str(WIFI_SETUP_NAMESPACE, key, buf);
    }

    return true;
}

bool wifi_setup_service_init(void)
{
    return storage_platform_init();
}

size_t wifi_setup_service_scan(wifi_scan_result_t *results, size_t max_results)
{
    return network_platform_scan(results, max_results);
}

bool wifi_setup_service_save_credentials(const char *ssid, const char *pass)
{
    if (ssid == 0 || pass == 0 || ssid[0] == '\0' || !wifi_setup_service_init()) {
        return false;
    }
    if (!storage_platform_set_str(WIFI_SETUP_NAMESPACE, KEY_SSID, ssid)) {
        return false;
    }
    if (!storage_platform_set_str(WIFI_SETUP_NAMESPACE, KEY_PASSWORD, pass)) {
        return false;
    }
    return true;
}

bool wifi_setup_service_get_saved_ssid(char *buf, size_t buf_len)
{
    return load_and_normalize_string(KEY_SSID, buf, buf_len) && buf[0] != '\0';
}

bool wifi_setup_service_get_saved_password(char *buf, size_t buf_len)
{
    return load_and_normalize_string(KEY_PASSWORD, buf, buf_len);
}
