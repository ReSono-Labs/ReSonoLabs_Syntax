idf_component_register(
    SRCS "provider_state_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES provider_runtime_api state runtime_control
)
