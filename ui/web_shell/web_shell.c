#include "web_shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_platform.h"
#include "app.h"
#include "board_registry.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_platform.h"
#include "orb_service.h"
#include "ota_service.h"
#include "power_platform.h"
#include "provider_web_bridge.h"
#include "runtime_control.h"
#include "security_auth.h"
#include "settings_service.h"
#include "state.h"
#include "ui_shell.h"
#include "orb_widget.h"
#include "web_request_utils.h"
#include "wifi_setup_service.h"

#define WEB_BUF_SMALL 128
#define WEB_BUF_MEDIUM 512
#define WEB_BUF_LARGE 2048
#define WIFI_SCAN_LIMIT 12

static const char *TAG = "web_shell";

static const char *s_setup_index_html_prefix =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Device Setup</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:460px;margin:24px auto;padding:0 16px;background:#f4f7fb;color:#102033}"
    "main{background:#ffffff;border-radius:20px;padding:22px;box-shadow:0 18px 48px rgba(15,23,42,.12)}"
    "h1{margin:0 0 6px 0;font-size:28px}p{color:#475569;line-height:1.5}"
    "button,input,select{width:100%;box-sizing:border-box;border-radius:12px;font-size:16px}"
    "button{border:none;padding:12px 14px;background:#0f766e;color:#fff;font-weight:700;margin-top:10px}"
    "input,select{padding:12px 14px;border:1px solid #cbd5e1;background:#fff;color:#0f172a;margin-top:8px}"
    ".muted{font-size:13px;color:#64748b}.status{white-space:pre-wrap;font-size:14px;color:#334155;margin-top:10px}"
    "</style></head><body><main>"
    "<h1>Device Setup</h1>"
    "<p>Connect this device to Wi-Fi to finish first-time setup.</p>"
    "<label class='muted'>Wi-Fi Network</label>"
    "<select id='ssid'>%s</select>"
    "<button type='button' id='scan_btn' onclick='window.location.reload()'>Scan Networks</button>"
    "<label class='muted'>Or enter SSID manually</label>"
    "<input id='manual_ssid' placeholder='Network name'>"
    "<label class='muted'>Password</label>"
    "<input id='pass' type='password' placeholder='Password'>"
    "<button onclick='saveWifi()'>Save Wi-Fi and Connect</button>"
    "<div id='status' class='status'>%s</div>"
    "</main><script>"
    "function chosenSsid(){const s=document.getElementById('ssid');const m=document.getElementById('manual_ssid').value.trim();return m||s.value||'';}"
    "async function saveWifi(){const ssid=chosenSsid();const pass=document.getElementById('pass').value;"
    "if(!ssid){document.getElementById('status').textContent='SSID required';return;}"
    "document.getElementById('status').textContent='Saving and connecting...';"
    "const body='ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass);"
    "const r=await fetch('/setup/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});"
    "document.getElementById('status').textContent=await r.text();}"
    "</script></body></html>";

static const char *s_index_html_prefix =
    "<!DOCTYPE html><html><head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Device Control</title>"
    "<style>"
    "body{font-family:sans-serif;max-width:760px;margin:20px auto;padding:0 14px;background:#101418;color:#edf2f7}"
    "h1,h2{margin:8px 0 12px 0}section{background:#17212b;border-radius:10px;padding:14px;margin:12px 0}"
    "input,button{width:100%;box-sizing:border-box;padding:10px;margin:6px 0;border-radius:8px;border:1px solid #334155}"
    "input{background:#0f1720;color:#edf2f7}button{background:#2563eb;color:#fff;border:none;font-weight:600}"
    ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px}.muted{color:#94a3b8;font-size:13px;white-space:pre-wrap}"
    "</style></head><body>"
    "<h1>Device Control</h1>"
    "<section><h2>Desk Bot Locked</h2><div class='muted'>Enter the 6-digit Dev PIN found in the device Info Drawer.</div>"
    "<input id='pin' inputmode='numeric' maxlength='6' placeholder='6-digit PIN'><button onclick='unlock()'>Unlock</button>"
    "<div id='auth' class='muted'></div></section>"
    "<section><h2>Status</h2><button onclick='loadStatus()'>Refresh Status</button><div id='status' class='muted'></div></section>"
    "<section><h2>Wi-Fi</h2><button onclick='scanWifi()'>Scan Networks</button><div id='scan' class='muted'></div>"
    "<input id='ssid' placeholder='SSID'><input id='pass' type='password' placeholder='Password'>"
    "<button onclick='saveWifi()'>Save Wi-Fi</button><div id='wifi' class='muted'></div></section>"
    "<section><h2>Settings</h2><div class='row'><input id='volume' type='number' min='0' max='100' placeholder='Volume'>"
    "<input id='brightness' type='number' min='5' max='100' placeholder='Brightness'></div>"
    "<button onclick='saveSettings()'>Save Settings</button><div id='settings' class='muted'></div></section>"
    "<section><h2>Orb</h2><div class='row'><input id='theme_id' type='number' min='0' max='255' placeholder='Theme ID'>"
    "<input id='global_speed' type='number' step='0.01' placeholder='Global Speed'></div>"
    "<button onclick='loadOrb()'>Load Orb</button><button onclick='saveOrb()'>Save Orb</button><div id='orb' class='muted'></div></section>";

static const char *s_index_html_suffix =
    "<section><h2>OTA</h2><input id='ota' type='file'><button onclick='uploadOta()'>Upload OTA</button><div id='ota_status' class='muted'></div></section>"
    "<script>"
    "let token=localStorage.getItem('deskbot_dev_pin')||'';"
    "function persistToken(value){token=value||''; if(token){localStorage.setItem('deskbot_dev_pin',token);}else{localStorage.removeItem('deskbot_dev_pin');}}"
    "function headers(extra){const h=extra||{}; if(token) h['Authorization']='Bearer '+token; return h;}"
    "function show(id,msg){document.getElementById(id).textContent=msg;}"
    "async function fetchAuthed(url,options){const r=await fetch(url,options); if(r.status===401){persistToken(''); show('auth','Unlock required');} return r;}"
    "async function unlock(){const pin=document.getElementById('pin').value.trim();"
    "const body='pin='+encodeURIComponent(pin);"
    "const r=await fetch('/api/auth',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});"
    "const t=await r.text(); if(!r.ok){show('auth',t);return;} persistToken(pin); show('auth','Unlocked'); if(window.startOpenClawStatusPolling) window.startOpenClawStatusPolling();}"
    "async function loadStatus(){const r=await fetchAuthed('/api/status',{headers:headers()}); const t=await r.text(); show('status',t);}"
    "async function scanWifi(){show('scan','Scanning...');"
    "try{const r=await fetchAuthed('/api/wifi/scan',{headers:headers()}); const t=await r.text(); if(!r.ok){show('scan',t);return;} show('scan',t);}"
    "catch(e){show('scan','Scan failed. Connection may have dropped during AP-mode scan.');}}"
    "async function saveWifi(){const body='ssid='+encodeURIComponent(document.getElementById('ssid').value)+'&pass='+encodeURIComponent(document.getElementById('pass').value);"
    "const r=await fetchAuthed('/api/wifi/save',{method:'POST',headers:headers({'Content-Type':'application/x-www-form-urlencoded'}),body}); show('wifi',await r.text());}"
    "async function saveSettings(){const body='volume='+encodeURIComponent(document.getElementById('volume').value)+'&brightness='+encodeURIComponent(document.getElementById('brightness').value);"
    "const r=await fetchAuthed('/api/settings',{method:'POST',headers:headers({'Content-Type':'application/x-www-form-urlencoded'}),body}); show('settings',await r.text());}"
    "async function loadOrb(){const r=await fetchAuthed('/api/orb',{headers:headers()}); const t=await r.text(); show('orb',t); try{const j=JSON.parse(t);document.getElementById('theme_id').value=j.theme_id;document.getElementById('global_speed').value=j.global_speed;}catch(e){}}"
    "async function saveOrb(){const body='theme_id='+encodeURIComponent(document.getElementById('theme_id').value)+'&global_speed='+encodeURIComponent(document.getElementById('global_speed').value);"
    "const r=await fetchAuthed('/api/orb',{method:'POST',headers:headers({'Content-Type':'application/x-www-form-urlencoded'}),body}); show('orb',await r.text());}"
    "%s"
    "async function uploadOta(){const f=document.getElementById('ota').files[0]; if(!f){show('ota_status','Select a firmware file first'); return;}"
    "const r=await fetchAuthed('/api/ota',{method:'POST',headers:headers({'Content-Type':'application/octet-stream'}),body:f}); show('ota_status',await r.text());}"
    "if(token){show('auth','Unlocked'); if(window.startOpenClawStatusPolling) window.startOpenClawStatusPolling();}"
    "</script></body></html>";

static bool web_shell_initial_setup_required(void)
{
    char ssid[64];

    memset(ssid, 0, sizeof(ssid));
    return !wifi_setup_service_get_saved_ssid(ssid, sizeof(ssid));
}

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

static esp_err_t handle_setup_index(httpd_req_t *req)
{
    wifi_scan_result_t results[WIFI_SCAN_LIMIT];
    char options_html[WEB_BUF_LARGE];
    char options_label[1024];
    const char *status_text = "Select a network or enter one manually.";
    char *html;
    size_t used = 0;
    size_t count;
    size_t html_len;

    ESP_LOGI(TAG, "Setup page requested from %s", req->uri ? req->uri : "?");
    memset(results, 0, sizeof(results));
    memset(options_html, 0, sizeof(options_html));
    count = wifi_setup_service_scan(results, WIFI_SCAN_LIMIT);
    vTaskDelay(pdMS_TO_TICKS(800));
    ESP_LOGI(TAG, "Setup page scan completed with %u networks", (unsigned)count);

    if (count == 0) {
        status_text = "No networks found. You can still enter the SSID manually.";
        used += snprintf(options_html + used,
                         sizeof(options_html) - used,
                         "<option value=''>No networks found</option>");
    } else {
        for (size_t i = 0; i < count && used < sizeof(options_html); ++i) {
            char escaped_ssid[64 * 6];
            size_t escaped_len;

            memset(escaped_ssid, 0, sizeof(escaped_ssid));
            escaped_len = append_html_escaped(escaped_ssid, sizeof(escaped_ssid), results[i].ssid);
            if (escaped_len == 0) {
                continue;
            }

            snprintf(options_label, sizeof(options_label), "%s (%ld dBm)", results[i].ssid, (long)results[i].rssi);
            used += snprintf(options_html + used, sizeof(options_html) - used, "<option value='");
            used += append_html_escaped(options_html + used, sizeof(options_html) - used, results[i].ssid);
            used += snprintf(options_html + used, sizeof(options_html) - used, "'>");
            used += append_html_escaped(options_html + used, sizeof(options_html) - used, options_label);
            used += snprintf(options_html + used, sizeof(options_html) - used, "</option>");
        }
    }

    html_len = strlen(s_setup_index_html_prefix) + strlen(options_html) + strlen(status_text) + 1;
    html = malloc(html_len);
    if (html == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "html alloc failed");
    }

    snprintf(html, html_len, s_setup_index_html_prefix, options_html, status_text);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    free(html);
    return ESP_OK;
}

static esp_err_t handle_index(httpd_req_t *req)
{
    const char *provider_html = provider_web_bridge_get_section_html();
    const char *provider_js = provider_web_bridge_get_section_js();
    size_t html_len;
    char *html;

    if (web_shell_initial_setup_required()) {
        return handle_setup_index(req);
    }

    if (provider_html == NULL) {
        provider_html = "";
    }
    if (provider_js == NULL) {
        provider_js = "";
    }

    html_len = strlen(s_index_html_prefix) + strlen(provider_html) + strlen(s_index_html_suffix) + strlen(provider_js) + 1;
    html = malloc(html_len);
    if (html == NULL) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "html alloc failed");
    }

    snprintf(html, html_len, "%s%s", s_index_html_prefix, provider_html);
    snprintf(html + strlen(html), html_len - strlen(html), s_index_html_suffix, provider_js);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
    free(html);
    return ESP_OK;
}

static esp_err_t handle_setup_scan(httpd_req_t *req)
{
    wifi_scan_result_t results[WIFI_SCAN_LIMIT];
    size_t count;
    char buf[WEB_BUF_LARGE];
    size_t used = 0;

    if (!web_shell_initial_setup_required()) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "setup complete");
    }

    memset(results, 0, sizeof(results));
    count = wifi_setup_service_scan(results, WIFI_SCAN_LIMIT);
    /* AP+STA scans can interrupt hotspot TCP briefly. Give the client time to recover. */
    vTaskDelay(pdMS_TO_TICKS(800));
    used += snprintf(buf + used, sizeof(buf) - used, "{\"networks\":[");
    for (size_t i = 0; i < count && used < sizeof(buf); ++i) {
        used += snprintf(buf + used,
                         sizeof(buf) - used,
                         "%s{\"ssid\":\"%s\",\"rssi\":%ld}",
                         i > 0 ? "," : "",
                         results[i].ssid,
                         (long)results[i].rssi);
    }
    snprintf(buf + used, sizeof(buf) - used, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t handle_setup_wifi_save(httpd_req_t *req)
{
    char body[WEB_BUF_SMALL];
    char ssid[64];
    char pass[64];
    bool connected;

    if (!web_shell_initial_setup_required()) {
        return httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "setup complete");
    }
    if (web_request_recv_body(req, body, sizeof(body)) < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body required");
    }

    memset(ssid, 0, sizeof(ssid));
    memset(pass, 0, sizeof(pass));
    web_request_extract_decoded_field(body, "ssid", ssid, sizeof(ssid));
    web_request_extract_decoded_field(body, "pass", pass, sizeof(pass));
    if (!wifi_setup_service_save_credentials(ssid, pass)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    }

    connected = network_platform_connect_sta(ssid, pass);
    ui_shell_sync_from_services();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req,
                       connected
                           ? "Wi-Fi saved. Device is connecting now. Reload this page from the device IP after it joins your network."
                           : "Wi-Fi saved, but the connection did not complete yet. Keep using the setup hotspot and try again.");
    return ESP_OK;
}

static esp_err_t handle_auth(httpd_req_t *req)
{
    char body[WEB_BUF_SMALL];
    char pin[16];
    char bearer[32];

    if (web_request_recv_body(req, body, sizeof(body)) < 0 ||
        !web_request_extract_field(body, "pin", pin, sizeof(pin))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "pin required");
    }

    snprintf(bearer, sizeof(bearer), "Bearer %s", pin);
    if (!security_auth_authorize_bearer(bearer)) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "invalid pin");
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t handle_status(httpd_req_t *req)
{
    char ssid[64];
    char ip[64];
    uint8_t volume = 0;
    uint8_t brightness = 0;
    power_status_t power;
    runtime_snapshot_t runtime;
    const board_profile_t *board;
    char buf[WEB_BUF_LARGE];

    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }

    memset(ssid, 0, sizeof(ssid));
    memset(ip, 0, sizeof(ip));
    memset(&power, 0, sizeof(power));
    memset(&runtime, 0, sizeof(runtime));
    board = app_get_active_board();
    wifi_setup_service_get_saved_ssid(ssid, sizeof(ssid));
    network_platform_get_ip(ip, sizeof(ip));
    settings_service_get_volume(&volume);
    settings_service_get_brightness(&brightness);
    power_platform_get_status(&power);
    runtime_control_get_snapshot(&runtime);

    snprintf(buf, sizeof(buf),
             "{\n"
             "  \"state\": %d,\n"
             "  \"board_id\": \"%s\",\n"
             "  \"board_target\": %d,\n"
             "  \"ssid\": \"%s\",\n"
             "  \"ip\": \"%s\",\n"
             "  \"volume\": %u,\n"
             "  \"brightness\": %u,\n"
             "  \"display_width\": %u,\n"
             "  \"display_height\": %u,\n"
             "  \"has_touch\": %s,\n"
             "  \"has_microphone\": %s,\n"
             "  \"has_speaker\": %s,\n"
             "  \"has_battery_monitor\": %s,\n"
             "  \"has_display_backlight_control\": %s,\n"
             "  \"supports_light_sleep\": %s,\n"
             "  \"supports_deep_sleep\": %s,\n"
             "  \"has_wifi\": %s,\n"
             "  \"has_bluetooth\": %s,\n"
             "  \"has_usb_device\": %s,\n"
             "  \"has_usb_power_sense\": %s,\n"
             "  \"battery_present\": %s,\n"
             "  \"battery_percent\": %d,\n"
             "  \"usb_connected\": %s,\n"
             "  \"runtime_available\": %s,\n"
             "  \"runtime_status\": %d,\n"
             "  \"runtime_detail\": \"%s\"\n"
             "}\n",
             (int)app_state_get(),
             (board != NULL && board->board_id != NULL) ? board->board_id : "",
             board != NULL ? (int)board->target : 0,
             ssid[0] ? ssid : "",
             ip[0] ? ip : "",
             (unsigned)volume,
             (unsigned)brightness,
             board != NULL ? (unsigned)board->layout.screen_width : 0U,
             board != NULL ? (unsigned)board->layout.screen_height : 0U,
             (board != NULL && board->capabilities.has_touch) ? "true" : "false",
             (board != NULL && board->capabilities.has_microphone) ? "true" : "false",
             (board != NULL && board->capabilities.has_speaker) ? "true" : "false",
             (board != NULL && board->capabilities.has_battery_monitor) ? "true" : "false",
             (board != NULL && board->capabilities.has_display_backlight_control) ? "true" : "false",
             (board != NULL && board->capabilities.supports_light_sleep) ? "true" : "false",
             (board != NULL && board->capabilities.supports_deep_sleep) ? "true" : "false",
             (board != NULL && board->capabilities.has_wifi) ? "true" : "false",
             (board != NULL && board->capabilities.has_bluetooth) ? "true" : "false",
             (board != NULL && board->capabilities.has_usb_device) ? "true" : "false",
             (board != NULL && board->capabilities.has_usb_power_sense) ? "true" : "false",
             power.battery_present ? "true" : "false",
             power.battery_percent,
             power.usb_connected ? "true" : "false",
             runtime_control_is_available() ? "true" : "false",
             (int)runtime.status,
             runtime.status_detail);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t handle_boards(httpd_req_t *req)
{
    const board_profile_t *active_board;
    char buf[WEB_BUF_LARGE];
    size_t used = 0;
    size_t count;

    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }

    active_board = app_get_active_board();
    count = board_registry_count();
    used += snprintf(buf + used, sizeof(buf) - used, "{\n  \"boards\": [\n");
    for (size_t i = 0; i < count && used < sizeof(buf); ++i) {
        const board_profile_t *board = board_registry_get_at(i);

        if (board == NULL) {
            continue;
        }

        used += snprintf(buf + used,
                         sizeof(buf) - used,
                         "    {\"board_id\":\"%s\",\"label\":\"%s\",\"target\":%d,\"display_width\":%u,\"display_height\":%u,"
                         "\"has_touch\":%s,\"has_microphone\":%s,\"has_speaker\":%s,\"has_battery_monitor\":%s,"
                         "\"has_display_backlight_control\":%s,\"supports_light_sleep\":%s,\"supports_deep_sleep\":%s,"
                         "\"has_wifi\":%s,\"has_bluetooth\":%s,\"has_usb_device\":%s,\"has_usb_power_sense\":%s,"
                         "\"active\":%s}%s\n",
                         board->board_id ? board->board_id : "",
                         board->board_label ? board->board_label : "",
                         (int)board->target,
                         (unsigned)board->layout.screen_width,
                         (unsigned)board->layout.screen_height,
                         board->capabilities.has_touch ? "true" : "false",
                         board->capabilities.has_microphone ? "true" : "false",
                         board->capabilities.has_speaker ? "true" : "false",
                         board->capabilities.has_battery_monitor ? "true" : "false",
                         board->capabilities.has_display_backlight_control ? "true" : "false",
                         board->capabilities.supports_light_sleep ? "true" : "false",
                         board->capabilities.supports_deep_sleep ? "true" : "false",
                         board->capabilities.has_wifi ? "true" : "false",
                         board->capabilities.has_bluetooth ? "true" : "false",
                         board->capabilities.has_usb_device ? "true" : "false",
                         board->capabilities.has_usb_power_sense ? "true" : "false",
                         (active_board != NULL && active_board == board) ? "true" : "false",
                         (i + 1U < count) ? "," : "");
    }
    snprintf(buf + used, sizeof(buf) - used, "  ]\n}\n");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t handle_ui_layout(httpd_req_t *req)
{
    ui_shell_layout_t layout;
    char buf[WEB_BUF_LARGE];

    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }
    if (!ui_shell_get_layout(&layout)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "layout unavailable");
    }

    snprintf(buf, sizeof(buf),
             "{\n"
             "  \"display_shape\": %d,\n"
             "  \"screen_width\": %u,\n"
             "  \"screen_height\": %u,\n"
             "  \"primary_visual_rect\": {\"x\": %d, \"y\": %d, \"width\": %u, \"height\": %u},\n"
             "  \"state_label_center_x\": %d,\n"
             "  \"state_label_baseline_y\": %d,\n"
             "  \"drawer_rect\": {\"x\": %d, \"y\": %d, \"width\": %u, \"height\": %u},\n"
             "  \"drawer_hidden_y\": %d,\n"
             "  \"drawer_open_y\": %d,\n"
             "  \"drawer_trigger_zone\": %u\n"
             "}\n",
             (int)layout.display_shape,
             (unsigned)layout.screen_width,
             (unsigned)layout.screen_height,
             (int)layout.primary_visual_rect.x,
             (int)layout.primary_visual_rect.y,
             (unsigned)layout.primary_visual_rect.width,
             (unsigned)layout.primary_visual_rect.height,
             (int)layout.state_label_center_x,
             (int)layout.state_label_baseline_y,
             (int)layout.drawer_rect.x,
             (int)layout.drawer_rect.y,
             (unsigned)layout.drawer_rect.width,
             (unsigned)layout.drawer_rect.height,
             (int)layout.drawer_hidden_y,
             (int)layout.drawer_open_y,
             (unsigned)layout.drawer_trigger_zone);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t handle_ui_shell_status(httpd_req_t *req)
{
    ui_shell_status_t status;
    char buf[WEB_BUF_LARGE];

    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }
    if (!ui_shell_get_status(&status)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "shell status unavailable");
    }

    snprintf(buf, sizeof(buf),
             "{\n"
             "  \"app_state\": %d,\n"
             "  \"presentation_visual\": %d,\n"
             "  \"locked\": %s,\n"
             "  \"drawer_visible\": %s,\n"
             "  \"runtime_available\": %s,\n"
             "  \"runtime_status\": %d,\n"
             "  \"runtime_status_detail\": \"%s\"\n"
             "}\n",
             (int)status.app_state,
             (int)status.presentation.visual,
             status.locked ? "true" : "false",
             status.drawer_visible ? "true" : "false",
             status.runtime_available ? "true" : "false",
             (int)status.runtime_status,
             status.runtime_status_detail);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t handle_ui_drawer(httpd_req_t *req)
{
    drawer_snapshot_t drawer;
    char buf[WEB_BUF_LARGE];

    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }
    if (!ui_shell_get_drawer_snapshot(&drawer)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "drawer unavailable");
    }

    snprintf(buf, sizeof(buf),
             "{\n"
             "  \"pin\": \"%s\",\n"
             "  \"ip\": \"%s\",\n"
             "  \"ssid\": \"%s\",\n"
             "  \"volume\": %d,\n"
             "  \"battery_present\": %s,\n"
             "  \"battery_percent\": %d,\n"
             "  \"usb_connected\": %s\n"
             "}\n",
             drawer.pin,
             drawer.ip,
             drawer.ssid,
             drawer.volume,
             drawer.battery_present ? "true" : "false",
             drawer.battery_percent,
             drawer.usb_connected ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t handle_ui_orb(httpd_req_t *req)
{
    orb_widget_snapshot_t orb;
    char buf[WEB_BUF_LARGE];

    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }
    if (!orb_widget_get_snapshot(&orb)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "orb snapshot unavailable");
    }

    snprintf(buf, sizeof(buf),
             "{\n"
             "  \"initialized\": %s,\n"
             "  \"visual\": %d,\n"
             "  \"has_config\": %s,\n"
             "  \"theme_id\": %u,\n"
             "  \"global_speed\": %.3f\n"
             "}\n",
             orb.initialized ? "true" : "false",
             (int)orb.visual,
             orb.has_config ? "true" : "false",
             (unsigned)orb.config.theme_id,
             (double)orb.config.global_speed);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t handle_wifi_scan(httpd_req_t *req)
{
    wifi_scan_result_t results[WIFI_SCAN_LIMIT];
    size_t count;
    char buf[WEB_BUF_LARGE];
    size_t used = 0;

    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }

    memset(results, 0, sizeof(results));
    count = wifi_setup_service_scan(results, WIFI_SCAN_LIMIT);
    /* Match donor behavior: avoid dropping the response immediately after a blocking scan. */
    vTaskDelay(pdMS_TO_TICKS(800));
    used += snprintf(buf + used, sizeof(buf) - used, "{\n  \"networks\": [\n");
    for (size_t i = 0; i < count && used < sizeof(buf); ++i) {
        used += snprintf(buf + used, sizeof(buf) - used,
                         "    {\"ssid\":\"%s\",\"rssi\":%ld}%s\n",
                         results[i].ssid,
                         (long)results[i].rssi,
                         (i + 1U < count) ? "," : "");
    }
    snprintf(buf + used, sizeof(buf) - used, "  ]\n}\n");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t handle_wifi_save(httpd_req_t *req)
{
    char body[WEB_BUF_SMALL];
    char ssid[64];
    char pass[64];
    bool connected;

    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }
    if (web_request_recv_body(req, body, sizeof(body)) < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body required");
    }

    memset(ssid, 0, sizeof(ssid));
    memset(pass, 0, sizeof(pass));
    web_request_extract_decoded_field(body, "ssid", ssid, sizeof(ssid));
    web_request_extract_decoded_field(body, "pass", pass, sizeof(pass));
    if (!wifi_setup_service_save_credentials(ssid, pass)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    }
    connected = network_platform_connect_sta(ssid, pass);
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, connected ? "wifi saved and connect attempted successfully" : "wifi saved but connect attempt failed");
    return ESP_OK;
}

static esp_err_t handle_settings_save(httpd_req_t *req)
{
    char body[WEB_BUF_SMALL];
    char value[16];

    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }
    if (web_request_recv_body(req, body, sizeof(body)) < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body required");
    }

    if (web_request_extract_field(body, "volume", value, sizeof(value)) && value[0] != '\0') {
        uint8_t volume = (uint8_t)atoi(value);
        settings_service_set_volume(volume);
        audio_platform_set_volume(volume);
    }
    if (web_request_extract_field(body, "brightness", value, sizeof(value)) && value[0] != '\0') {
        settings_service_set_brightness((uint8_t)atoi(value));
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "settings saved");
    return ESP_OK;
}

static esp_err_t handle_orb_get(httpd_req_t *req)
{
    orb_config_t config;
    char buf[256];

    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }
    if (!orb_service_get_config(&config)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "orb unavailable");
    }

    snprintf(buf, sizeof(buf),
             "{\n  \"theme_id\": %u,\n  \"global_speed\": %.3f\n}\n",
             (unsigned)config.theme_id,
             (double)config.global_speed);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t handle_orb_save(httpd_req_t *req)
{
    char body[WEB_BUF_SMALL];
    char value[32];
    orb_config_t config;
    uint8_t original_theme_id;

    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }
    if (web_request_recv_body(req, body, sizeof(body)) < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body required");
    }
    if (!orb_service_get_config(&config)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "orb unavailable");
    }
    original_theme_id = config.theme_id;

    if (web_request_extract_field(body, "theme_id", value, sizeof(value)) && value[0] != '\0') {
        config.theme_id = (uint8_t)atoi(value);
        if (config.theme_id != original_theme_id &&
            !orb_widget_theme_apply_defaults(config.theme_id, &config)) {
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown orb theme");
        }
    }
    if (web_request_extract_field(body, "global_speed", value, sizeof(value)) && value[0] != '\0') {
        config.global_speed = (float)atof(value);
    }
    if (!orb_service_set_config(&config)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "orb save failed");
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "orb saved");
    return ESP_OK;
}

static esp_err_t handle_ota(httpd_req_t *req)
{
    uint8_t chunk[1024];
    int remaining;

    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }
    if (req->content_len <= 0 || !ota_service_begin((size_t)req->content_len)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid ota upload");
    }

    remaining = req->content_len;
    while (remaining > 0) {
        int received = httpd_req_recv(req, (char *)chunk, remaining > (int)sizeof(chunk) ? (int)sizeof(chunk) : remaining);
        if (received <= 0 || !ota_service_write_chunk(chunk, (size_t)received)) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota write failed");
        }
        remaining -= received;
    }
    if (!ota_service_finish()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ota finalize failed");
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "ota uploaded");
    return ESP_OK;
}

bool web_shell_register_routes(void *server)
{
    httpd_handle_t handle = (httpd_handle_t)server;
    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = handle_index};
    httpd_uri_t setup_scan = {.uri = "/setup/scan", .method = HTTP_GET, .handler = handle_setup_scan};
    httpd_uri_t setup_wifi = {.uri = "/setup/wifi", .method = HTTP_POST, .handler = handle_setup_wifi_save};
    httpd_uri_t auth = {.uri = "/api/auth", .method = HTTP_POST, .handler = handle_auth};
    httpd_uri_t status = {.uri = "/api/status", .method = HTTP_GET, .handler = handle_status};
    httpd_uri_t boards = {.uri = "/api/boards", .method = HTTP_GET, .handler = handle_boards};
    httpd_uri_t ui_layout = {.uri = "/api/ui/layout", .method = HTTP_GET, .handler = handle_ui_layout};
    httpd_uri_t ui_shell_status = {.uri = "/api/ui/shell", .method = HTTP_GET, .handler = handle_ui_shell_status};
    httpd_uri_t ui_drawer = {.uri = "/api/ui/drawer", .method = HTTP_GET, .handler = handle_ui_drawer};
    httpd_uri_t ui_orb = {.uri = "/api/ui/orb", .method = HTTP_GET, .handler = handle_ui_orb};
    httpd_uri_t wifi_scan = {.uri = "/api/wifi/scan", .method = HTTP_GET, .handler = handle_wifi_scan};
    httpd_uri_t wifi_save = {.uri = "/api/wifi/save", .method = HTTP_POST, .handler = handle_wifi_save};
    httpd_uri_t settings = {.uri = "/api/settings", .method = HTTP_POST, .handler = handle_settings_save};
    httpd_uri_t orb_get = {.uri = "/api/orb", .method = HTTP_GET, .handler = handle_orb_get};
    httpd_uri_t orb_save = {.uri = "/api/orb", .method = HTTP_POST, .handler = handle_orb_save};
    httpd_uri_t ota = {.uri = "/api/ota", .method = HTTP_POST, .handler = handle_ota};

    if (handle == NULL) {
        return false;
    }

    if (httpd_register_uri_handler(handle, &root) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &setup_scan) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &setup_wifi) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &auth) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &status) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &boards) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &ui_layout) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &ui_shell_status) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &ui_drawer) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &ui_orb) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &wifi_scan) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &wifi_save) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &settings) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &orb_get) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &orb_save) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &ota) != ESP_OK) return false;
    return true;
}
