idf_component_register(
    SRCS "provider_runtime_api.c"
    INCLUDE_DIRS "include"
    REQUIRES runtime_control provider_session provider_terminal provider_ui_bridge
)
