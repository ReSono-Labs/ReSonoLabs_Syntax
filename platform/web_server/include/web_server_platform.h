#ifndef WEB_SERVER_PLATFORM_H
#define WEB_SERVER_PLATFORM_H

#include <stdbool.h>

typedef void *web_server_handle_t;

bool web_server_platform_start(web_server_handle_t *out_server);
web_server_handle_t web_server_platform_get_server(void);

#endif
