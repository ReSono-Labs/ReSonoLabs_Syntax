idf_component_register(
    SRCS "network_platform.c"
    INCLUDE_DIRS "include" "../../boards/common/include"
    REQUIRES esp_wifi esp_event esp_netif
)
