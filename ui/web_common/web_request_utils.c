#include "web_request_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "security_auth.h"

bool web_request_get_header(httpd_req_t *req, const char *name, char *buf, size_t buf_len)
{
    size_t len;

    if (req == NULL || name == NULL || buf == NULL || buf_len == 0) {
        return false;
    }

    buf[0] = '\0';
    len = httpd_req_get_hdr_value_len(req, name);
    if (len == 0 || len >= buf_len) {
        return false;
    }

    return httpd_req_get_hdr_value_str(req, name, buf, buf_len) == ESP_OK;
}

bool web_request_extract_field(const char *body, const char *key, char *out, size_t out_len)
{
    char pattern[48];
    const char *start;
    const char *end;
    size_t len;

    if (body == NULL || key == NULL || out == NULL || out_len == 0) {
        return false;
    }

    snprintf(pattern, sizeof(pattern), "%s=", key);
    start = strstr(body, pattern);
    if (start == NULL) {
        out[0] = '\0';
        return false;
    }

    start += strlen(pattern);
    end = strchr(start, '&');
    len = end ? (size_t)(end - start) : strlen(start);
    if (len >= out_len) {
        len = out_len - 1;
    }

    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

static void web_request_url_decode(char *dst, size_t dst_len, const char *src)
{
    size_t di = 0;

    if (dst == NULL || dst_len == 0) {
        return;
    }

    dst[0] = '\0';
    if (src == NULL) {
        return;
    }

    for (size_t i = 0; src[i] != '\0' && di + 1 < dst_len; ++i) {
        if (src[i] == '%' && src[i + 1] != '\0' && src[i + 2] != '\0') {
            char hex[3] = {src[i + 1], src[i + 2], '\0'};

            dst[di++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else if (src[i] == '+') {
            dst[di++] = ' ';
        } else {
            dst[di++] = src[i];
        }
    }

    dst[di] = '\0';
}

bool web_request_extract_decoded_field(const char *body, const char *key, char *out, size_t out_len)
{
    char raw[128];

    if (out == NULL || out_len == 0) {
        return false;
    }

    memset(raw, 0, sizeof(raw));
    if (!web_request_extract_field(body, key, raw, sizeof(raw))) {
        out[0] = '\0';
        return false;
    }

    web_request_url_decode(out, out_len, raw);
    return out[0] != '\0';
}

int web_request_recv_body(httpd_req_t *req, char *buf, size_t buf_len)
{
    int total = 0;

    if (req == NULL || buf == NULL || buf_len == 0 || req->content_len <= 0 || req->content_len >= (int)buf_len) {
        return -1;
    }

    while (total < req->content_len) {
        int ret = httpd_req_recv(req, buf + total, req->content_len - total);
        if (ret <= 0) {
            return -1;
        }
        total += ret;
    }

    buf[total] = '\0';
    return total;
}

bool web_request_ensure_authorized(httpd_req_t *req)
{
    char auth[80];

    if (!web_request_get_header(req, "Authorization", auth, sizeof(auth))) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Authorization required", HTTPD_RESP_USE_STRLEN);
        return false;
    }
    if (!security_auth_authorize_bearer(auth)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "Unauthorized", HTTPD_RESP_USE_STRLEN);
        return false;
    }

    security_auth_mark_activity();
    return true;
}
