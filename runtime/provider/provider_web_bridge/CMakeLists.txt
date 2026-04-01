idf_component_register(
    SRCS "provider_web_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server provider_runtime_api provider_transport web_common
)
