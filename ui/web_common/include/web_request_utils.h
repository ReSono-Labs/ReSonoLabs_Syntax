#ifndef WEB_REQUEST_UTILS_H
#define WEB_REQUEST_UTILS_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_http_server.h"

bool web_request_get_header(httpd_req_t *req, const char *name, char *buf, size_t buf_len);
bool web_request_extract_field(const char *body, const char *key, char *out, size_t out_len);
bool web_request_extract_decoded_field(const char *body, const char *key, char *out, size_t out_len);
int web_request_recv_body(httpd_req_t *req, char *buf, size_t buf_len);
bool web_request_ensure_authorized(httpd_req_t *req);

#endif
