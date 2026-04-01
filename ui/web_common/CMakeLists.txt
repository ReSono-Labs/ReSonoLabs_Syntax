idf_component_register(
    SRCS "web_request_utils.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server security
)
