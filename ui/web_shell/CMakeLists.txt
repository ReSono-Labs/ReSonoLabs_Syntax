idf_component_register(
    SRCS "web_shell.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server security wifi_setup settings orb ota network power state runtime_control audio provider_web_bridge app shell widgets web_common
)
