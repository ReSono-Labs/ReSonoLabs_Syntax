#ifndef PROVIDER_WEB_BRIDGE_H
#define PROVIDER_WEB_BRIDGE_H

#include <stdbool.h>

bool provider_web_bridge_register_routes(void *server);
const char *provider_web_bridge_get_section_html(void);
const char *provider_web_bridge_get_section_js(void);

#endif
