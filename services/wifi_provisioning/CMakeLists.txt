idf_component_register(
    SRCS "wifi_provisioning.c"
    INCLUDE_DIRS "include"
    REQUIRES network wifi_setup shell web_common esp_http_server esp_system
)
