#include "provider_web_bridge.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_http_server.h"
#include "provider_runtime_api.h"
#include "provider_storage.h"
#include "provider_transport.h"
#include "web_request_utils.h"

#define PROVIDER_WEB_BODY_MAX 512
#define PROVIDER_WEB_TOKEN_MAX 256

static const char *transport_state_name(provider_transport_state_t state)
{
    switch (state) {
    case PROVIDER_TRANSPORT_STATE_CONNECTING:
        return "CONNECTING";
    case PROVIDER_TRANSPORT_STATE_PAIRING:
        return "PAIRING";
    case PROVIDER_TRANSPORT_STATE_READY:
        return "READY";
    case PROVIDER_TRANSPORT_STATE_BUSY:
        return "BUSY";
    case PROVIDER_TRANSPORT_STATE_DISCONNECTED:
    default:
        return "DISCONNECTED";
    }
}

static const char *s_provider_section_html =
    "<section><h2>OpenClaw Pairing</h2>"
    "<div id='provider_status' class='muted'><i>Loading OpenClaw status...</i></div>"
    "<div class='muted'><b>ReSono Labs Syntax uses OpenClaw device auth flow</b></div>"
    "<div class='muted'>OpenClaw IP / Host: <span id='provider_host'>-</span></div>"
    "<button onclick='refreshOpenClawStatus()'>Refresh OpenClaw Status</button>"
    "<input id='provider_endpoint_host' placeholder='OpenClaw IP or host'>"
    "<input id='provider_endpoint_port' type='number' min='1' max='65535' placeholder='OpenClaw port'>"
    "<button onclick='saveOpenClawEndpoint()'>Save OpenClaw Endpoint</button>"
    "<input id='gateway_token' type='password' placeholder='Gateway Token'>"
    "<button onclick='saveProviderGatewayToken()'>Save Gateway Token</button>"
    "<div class='muted'>Saving the gateway token already starts the first OpenClaw connect/pair attempt if no device token exists.</div>"
    "<button onclick='reconnectOpenClaw()'>Reconnect OpenClaw</button>"
    "<button onclick='forgetOpenClawDeviceToken()'>Forget Device Token</button>"
    "<button onclick='resetOpenClaw()'>Full OpenClaw Reset (Clear Gateway + Device Tokens)</button>"
    "<div class='muted'>Use <b>Reconnect OpenClaw</b> after approving a pending ReSono Labs Syntax device in OpenClaw. Do not re-pair unless you intend to create a new device identity.</div>"
    "<div id='provider' class='muted'></div></section>";

static const char *s_provider_section_js =
    "let openClawStatusPoll=0;"
    "async function refreshOpenClawStatus(){"
    "try{const r=await fetchAuthed('/api/provider/status',{headers:headers()});"
    "const t=await r.text();"
    "if(!r.ok){show('provider_status',t);return;}"
    "const oc=JSON.parse(t);"
    "const pair=oc.pair_code&&oc.pair_code.length?oc.pair_code:'-';"
    "const host=oc.host&&oc.host.length?oc.host:'-';"
    "document.getElementById('provider_host').textContent=oc.port?host+':'+oc.port:host;"
    "document.getElementById('provider_status').innerHTML='State: <b>'+oc.state+'</b> &nbsp; Pair Code: <b>'+pair+'</b><br>Auth Stage: '+(oc.auth_stage||'-')+' &nbsp; Device Token: '+(oc.has_token?'saved':'none')+' &nbsp; Gateway Token: '+(oc.has_gateway_token?'saved':'none');"
    "}catch(e){show('provider_status','OpenClaw status unavailable');}}"
    "async function saveOpenClawEndpoint(){const host=document.getElementById('provider_endpoint_host').value.trim();"
    "const port=document.getElementById('provider_endpoint_port').value.trim();"
    "const body='host='+encodeURIComponent(host)+'&port='+encodeURIComponent(port);"
    "const r=await fetchAuthed('/api/provider/endpoint',{method:'POST',headers:headers({'Content-Type':'application/x-www-form-urlencoded'}),body}); show('provider',await r.text()); refreshOpenClawStatus();}"
    "async function saveProviderGatewayToken(){const body='gateway_token='+encodeURIComponent(document.getElementById('gateway_token').value);"
    "const r=await fetchAuthed('/api/provider/gateway-token',{method:'POST',headers:headers({'Content-Type':'application/x-www-form-urlencoded'}),body}); show('provider',await r.text()); refreshOpenClawStatus();}"
    "async function reconnectOpenClaw(){const r=await fetchAuthed('/api/provider/reconnect',{method:'POST',headers:headers()}); show('provider',await r.text()); refreshOpenClawStatus();}"
    "async function forgetOpenClawDeviceToken(){const r=await fetchAuthed('/api/provider/forget-device-token',{method:'POST',headers:headers()}); show('provider',await r.text()); refreshOpenClawStatus();}"
    "async function resetOpenClaw(){const r=await fetchAuthed('/api/provider/full-reset',{method:'POST',headers:headers()}); show('provider',await r.text()); refreshOpenClawStatus();}"
    "function startOpenClawStatusPolling(){if(openClawStatusPoll)return;refreshOpenClawStatus();openClawStatusPoll=setInterval(refreshOpenClawStatus,3000);}"
    "document.addEventListener('DOMContentLoaded',()=>{if(localStorage.getItem('deskbot_dev_pin'))startOpenClawStatusPolling();});";

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

static esp_err_t handle_gateway_token(httpd_req_t *req)
{
    char body[PROVIDER_WEB_BODY_MAX];
    char token_raw[PROVIDER_WEB_TOKEN_MAX];
    char token[PROVIDER_WEB_TOKEN_MAX];

    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }
    if (web_request_recv_body(req, body, sizeof(body)) < 0 ||
        !web_request_extract_field(body, "gateway_token", token_raw, sizeof(token_raw))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "gateway_token missing");
    }
    url_decode(token, sizeof(token), token_raw);
    if (!provider_runtime_save_gateway_token(token)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OpenClaw gateway token saved. Reconnecting now.");
    return ESP_OK;
}

static esp_err_t handle_endpoint(httpd_req_t *req)
{
    char body[PROVIDER_WEB_BODY_MAX];
    char host_raw[96];
    char host[96];
    char port_value[16];
    long port = 0;

    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }
    if (web_request_recv_body(req, body, sizeof(body)) < 0) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Read error");
    }
    if (!web_request_extract_field(body, "host", host_raw, sizeof(host_raw))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "host missing");
    }
    if (!web_request_extract_field(body, "port", port_value, sizeof(port_value))) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "port missing");
    }

    url_decode(host, sizeof(host), host_raw);
    port = strtol(port_value, NULL, 10);
    if (host[0] == '\0' && port_value[0] == '\0') {
        if (!provider_runtime_clear_endpoint()) {
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "endpoint clear failed");
        }
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "OpenClaw endpoint cleared.");
        return ESP_OK;
    }
    if (host[0] == '\0' || port <= 0 || port > 65535) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid endpoint");
    }
    if (!provider_runtime_set_endpoint(host, (uint16_t)port)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "endpoint save failed");
    }
    if (!provider_runtime_reconnect()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "endpoint saved but reconnect failed");
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OpenClaw endpoint saved. Reconnecting now.");
    return ESP_OK;
}

static esp_err_t handle_status(httpd_req_t *req)
{
    runtime_snapshot_t snapshot;
    const char *auth_stage;
    const char *chat_block_reason;
    char saved_host[96];
    uint16_t saved_port = 0;
    char body[768];

    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }
    if (!provider_runtime_get_snapshot(&snapshot)) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status unavailable");
    }

    auth_stage = provider_transport_get_auth_stage();
    chat_block_reason = provider_transport_get_chat_block_reason();
    saved_host[0] = '\0';
    if (!(provider_storage_load_endpoint_host(saved_host, sizeof(saved_host)) &&
          provider_storage_load_endpoint_port(&saved_port))) {
        saved_host[0] = '\0';
        saved_port = 0;
    }

    snprintf(body, sizeof(body),
             "{"
             "\"host\":\"%s\","
             "\"port\":%u,"
             "\"state\":\"%s\","
             "\"auth_stage\":\"%s\","
             "\"pair_code\":\"%s\","
             "\"has_token\":%s,"
             "\"has_gateway_token\":%s,"
             "\"chat_block_remaining_ms\":%lld,"
             "\"chat_block_reason\":\"%s\""
             "}",
             saved_host,
             (unsigned)saved_port,
             transport_state_name(provider_transport_get_state()),
             auth_stage != NULL ? auth_stage : "",
             snapshot.pair_code,
             snapshot.has_device_token ? "true" : "false",
             snapshot.has_gateway_token ? "true" : "false",
             (long long)provider_transport_get_chat_block_remaining_ms(),
             chat_block_reason != NULL ? chat_block_reason : "");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, body);
    return ESP_OK;
}

static esp_err_t handle_reconnect(httpd_req_t *req)
{
    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }
    if (!provider_runtime_reconnect()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "reconnect failed");
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OpenClaw reconnect requested using the current device identity.");
    return ESP_OK;
}

static esp_err_t handle_forget_device_token(httpd_req_t *req)
{
    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }
    if (!provider_runtime_forget_device_token()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "forget failed");
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OpenClaw device token erased. Save the gateway token or reconnect when ready.");
    return ESP_OK;
}

static esp_err_t handle_full_reset(httpd_req_t *req)
{
    if (!web_request_ensure_authorized(req)) {
        return ESP_FAIL;
    }
    if (!provider_runtime_full_reset()) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "reset failed");
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "OpenClaw reset on ESP complete (gateway token + device token cleared). Connection stopped.");
    return ESP_OK;
}

bool provider_web_bridge_register_routes(void *server)
{
    httpd_handle_t handle = (httpd_handle_t)server;
    httpd_uri_t status = {.uri = "/api/provider/status", .method = HTTP_GET, .handler = handle_status};
    httpd_uri_t endpoint = {.uri = "/api/provider/endpoint", .method = HTTP_POST, .handler = handle_endpoint};
    httpd_uri_t gateway = {.uri = "/api/provider/gateway-token", .method = HTTP_POST, .handler = handle_gateway_token};
    httpd_uri_t reconnect = {.uri = "/api/provider/reconnect", .method = HTTP_POST, .handler = handle_reconnect};
    httpd_uri_t forget = {.uri = "/api/provider/forget-device-token", .method = HTTP_POST, .handler = handle_forget_device_token};
    httpd_uri_t reset = {.uri = "/api/provider/full-reset", .method = HTTP_POST, .handler = handle_full_reset};

    if (handle == NULL) {
        return false;
    }

    if (httpd_register_uri_handler(handle, &status) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &endpoint) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &gateway) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &reconnect) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &forget) != ESP_OK) return false;
    if (httpd_register_uri_handler(handle, &reset) != ESP_OK) return false;
    return true;
}

const char *provider_web_bridge_get_section_html(void)
{
    return s_provider_section_html;
}

const char *provider_web_bridge_get_section_js(void)
{
    return s_provider_section_js;
}
