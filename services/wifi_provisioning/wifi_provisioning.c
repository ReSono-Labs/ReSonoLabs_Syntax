#include "wifi_provisioning.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_platform.h"
#include "ui_shell.h"
#include "web_request_utils.h"
#include "wifi_setup_service.h"

#define PROV_BUF_SMALL 256
#define PROV_BUF_LARGE 4096
#define PROV_SCAN_LIMIT 12

static const char *TAG = "wifi_prov";

static const char *s_setup_page =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Device Setup</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:460px;margin:24px auto;padding:0 16px;background:#f4f7fb;color:#102033}"
    "main{background:#ffffff;border-radius:20px;padding:22px;box-shadow:0 18px 48px rgba(15,23,42,.12)}"
    "h1{margin:0 0 6px 0;font-size:28px}p{color:#475569;line-height:1.5}"
    "button,input,select{width:100%%;box-sizing:border-box;border-radius:12px;font-size:16px}"
    "button{border:none;padding:12px 14px;background:#0f766e;color:#fff;font-weight:700;margin-top:10px}"
    "input,select{padding:12px 14px;border:1px solid #cbd5e1;background:#fff;color:#0f172a;margin-top:8px}"
    ".muted{font-size:13px;color:#64748b}.status{white-space:pre-wrap;font-size:14px;color:#334155;margin-top:10px}"
    "</style></head><body><main>"
    "<h1>Device Setup</h1>"
    "<p>Connect this device to Wi-Fi to finish first-time setup.</p>"
    "<form method='POST' action='/save'>"
    "<label class='muted'>Wi-Fi Network</label>"
    "<select name='ssid'>%s</select>"
    "<button type='button' onclick='window.location.reload()'>Scan Networks</button>"
    "<label class='muted'>Or enter SSID manually</label>"
    "<input name='manual_ssid' placeholder='Network name'>"
    "<label class='muted'>Password</label>"
    "<input name='pass' type='password' placeholder='Password'>"
    "<button type='submit'>Save Wi-Fi and Connect</button>"
    "</form>"
    "<div class='status'>%s</div>"
    "</main></body></html>";

static const char *s_saved_page =
    "<!DOCTYPE html><html><body style='font-family:sans-serif;text-align:center;padding:40px'>"
    "<h2 style='color:#0c7c59'>Saved</h2>"
    "<p>The device will reboot and continue setup using the normal control UI.</p>"
    "</body></html>";

static httpd_handle_t s_server;

static size_t append_html_escaped(char *dst, size_t dst_len, const char *src)
{
    size_t used = 0;

    if (dst == NULL || dst_len == 0 || src == NULL) {
        return 0;
    }

    while (*src != '\0' && used + 1 < dst_len) {
        const char *escaped = NULL;
        char ch = *src++;

        switch (ch) {
        case '&':
            escaped = "&amp;";
            break;
        case '<':
            escaped = "&lt;";
            break;
        case '>':
            escaped = "&gt;";
            break;
        case '"':
            escaped = "&quot;";
            break;
        case '\'':
            escaped = "&#39;";
            break;
        default:
            dst[used++] = ch;
            continue;
        }

        while (*escaped != '\0' && used + 1 < dst_len) {
            dst[used++] = *escaped++;
        }
    }

    dst[used] = '\0';
    return used;
}

static void url_decode(char *dst, size_t dst_len, const char *src)
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

static bool extract_decoded_field(const char *body, const char *key, char *out, size_t out_len)
{
    char raw[128];

    memset(raw, 0, sizeof(raw));
    if (!web_request_extract_field(body, key, raw, sizeof(raw))) {
        if (out != NULL && out_len > 0) {
            out[0] = '\0';
        }
        return false;
    }

    url_decode(out, out_len, raw);
    return out != NULL && out[0] != '\0';
}

static esp_err_t handle_root(httpd_req_t *req)
{
    wifi_scan_result_t results[PROV_SCAN_LIMIT];
    char options_html[PROV_BUF_LARGE];
    const char *status = "Select a network or enter one manually.";
    char *page;
    size_t count;
    size_t used = 0;
    size_t page_len;

    ESP_LOGI(TAG, "Provisioning page requested");
    memset(results, 0, sizeof(results));
    memset(options_html, 0, sizeof(options_html));

    count = wifi_setup_service_scan(results, PROV_SCAN_LIMIT);
    vTaskDelay(pdMS_TO_TICKS(800));
    ESP_LOGI(TAG, "Provisioning scan completed with %u networks", (unsigned)count);

    if (count == 0) {
        status = "No networks found. You can still enter the SSID manually.";
        used += snprintf(options_html + used,
                         sizeof(options_html) - used,
                         "<option value=''>No networks found</option>");
    } else {
        for (size_t i = 0; i < count && used < sizeof(options_html); ++i) {
            char label[1024];

            snprintf(label, sizeof(label), "%s (%ld dBm)", results[i].ssid, (long)results[i].rssi);
            used += snprintf(options_html + used, sizeof(options_html) - used, "<option value='");
            used += append_html_escaped(options_html + used, sizeof(options_html) - used, results[i].ssid);
            used += snprintf(options_html + used, sizeof(options_html) - used, "'>");
            used += append_html_escaped(options_html + used, sizeof(options_html) - used, label);
            used += snprintf(options_html + used, sizeof(options_html) - used, "</option>");
        }
    }

    page_len = strlen(s_setup_page) + strlen(options_html) + strlen(status) + 1;
    page = malloc(page_len);
    if (page == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "page alloc failed");
    }

    snprintf(page, page_len, s_setup_page, options_html, status);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
    free(page);
    return ESP_OK;
}

static esp_err_t handle_save(httpd_req_t *req)
{
    char body[PROV_BUF_SMALL];
    char ssid[64];
    char manual_ssid[64];
    char pass[64];
    const char *selected_ssid = NULL;

    if (web_request_recv_body(req, body, sizeof(body)) < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body required");
    }

    memset(ssid, 0, sizeof(ssid));
    memset(manual_ssid, 0, sizeof(manual_ssid));
    memset(pass, 0, sizeof(pass));
    extract_decoded_field(body, "ssid", ssid, sizeof(ssid));
    extract_decoded_field(body, "manual_ssid", manual_ssid, sizeof(manual_ssid));
    extract_decoded_field(body, "pass", pass, sizeof(pass));

    selected_ssid = manual_ssid[0] != '\0' ? manual_ssid : ssid;
    if (selected_ssid == NULL || selected_ssid[0] == '\0') {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "ssid required");
    }
    if (!wifi_setup_service_save_credentials(selected_ssid, pass)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    }

    ESP_LOGI(TAG, "Provisioning credentials saved for ssid=%s", selected_ssid);
    if (network_platform_connect_sta(selected_ssid, pass)) {
        network_platform_stop_setup_ap();
    }
    ui_shell_sync_from_services();

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, s_saved_page);
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
    return ESP_OK;
}

bool wifi_provisioning_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handle_root,
    };
    httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = handle_save,
    };

    if (s_server != NULL) {
        return true;
    }

    cfg.stack_size = 8192;
    cfg.max_uri_handlers = 8;
    cfg.lru_purge_enable = true;
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning server");
        s_server = NULL;
        return false;
    }

    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &save);
    ESP_LOGI(TAG, "Provisioning server ready at http://192.168.4.1/");
    return true;
}
