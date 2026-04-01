#include "web_server_platform.h"

#include "esp_http_server.h"

static web_server_handle_t s_server;

bool web_server_platform_start(web_server_handle_t *out_server)
{
    httpd_handle_t server = (httpd_handle_t)s_server;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (server == NULL) {
        config.lru_purge_enable = true;
        config.max_uri_handlers = 32;
        if (httpd_start(&server, &config) != ESP_OK) {
            return false;
        }
        s_server = (web_server_handle_t)server;
    }

    if (out_server != 0) {
        *out_server = s_server;
    }
    return true;
}

web_server_handle_t web_server_platform_get_server(void)
{
    return s_server;
}
