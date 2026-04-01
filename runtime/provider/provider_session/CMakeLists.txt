idf_component_register(
    SRCS "provider_session.c"
    INCLUDE_DIRS "include"
    REQUIRES runtime_control provider_storage provider_transport
)
