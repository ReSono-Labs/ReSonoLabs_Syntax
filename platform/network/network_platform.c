#include "network_platform.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAILED_BIT    BIT1
#define SETUP_AP_PASSWORD  "abcdefgh"

static const char *TAG = "network_platform";

static bool s_initialized;
static bool s_ap_started;
static bool s_wifi_started;
static bool s_sta_configured;
static bool s_sta_connected;
static volatile bool s_scanning;
static board_profile_t s_board;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static EventGroupHandle_t s_wifi_events;

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void network_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (s_wifi_events == NULL) {
        return;
    }

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_connected = false;
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        if (s_scanning) {
            return;
        }
        xEventGroupSetBits(s_wifi_events, WIFI_FAILED_BIT);
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_sta_connected = true;
        if (esp_sntp_enabled()) {
            esp_sntp_stop();
        }
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_setservername(1, "time.google.com");
        esp_sntp_setservername(2, "time.apple.com");
        esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
        esp_sntp_init();
        ESP_LOGI(TAG, "SNTP sync started (pool, google, apple)");
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        return;
    }

    (void)event_data;
}

static bool network_init_stack(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err;

    if (s_initialized) {
        return true;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return false;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return false;
    }

    if (s_sta_netif == NULL) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }
    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }
    if (s_sta_netif == NULL || s_ap_netif == NULL) {
        return false;
    }

    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return false;
    }

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &network_event_handler, NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return false;
    }

    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &network_event_handler, NULL);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return false;
    }

    if (s_wifi_events == NULL) {
        s_wifi_events = xEventGroupCreate();
        if (s_wifi_events == NULL) {
            return false;
        }
    }

    s_initialized = true;
    return true;
}

static bool ensure_wifi_started(void)
{
    esp_err_t err;

    if (s_wifi_started) {
        return true;
    }

    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_WIFI_CONN && err != ESP_ERR_WIFI_NOT_INIT) {
        return false;
    }
    if (esp_wifi_set_ps(WIFI_PS_MIN_MODEM) != ESP_OK) {
        return false;
    }

    s_wifi_started = true;
    return true;
}

static void build_setup_ap_ssid(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return;
    }

    char mac[6];
    esp_read_mac((uint8_t *)mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(buf, buf_len, "ReSono Labs Syntax %02X%02X", mac[4], mac[5]);
}

bool network_platform_init(const board_profile_t *board)
{
    if (board == NULL || !board->capabilities.has_wifi) {
        return false;
    }

    s_board = *board;
    return network_init_stack();
}

bool network_platform_start_setup_ap(void)
{
    wifi_config_t ap_cfg;
    char ap_ssid[32];

    if (!network_init_stack()) {
        return false;
    }

    memset(&ap_cfg, 0, sizeof(ap_cfg));
    build_setup_ap_ssid(ap_ssid, sizeof(ap_ssid));
    strlcpy((char *)ap_cfg.ap.ssid, ap_ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(ap_ssid);
    ap_cfg.ap.channel = 1;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    strlcpy((char *)ap_cfg.ap.password, SETUP_AP_PASSWORD, sizeof(ap_cfg.ap.password));

    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) {
        return false;
    }
    if (esp_wifi_set_config(WIFI_IF_AP, &ap_cfg) != ESP_OK) {
        return false;
    }
    if (!ensure_wifi_started()) {
        return false;
    }

    s_ap_started = true;
    ESP_LOGI(TAG, "Setup AP started ssid=%s password=%s", ap_ssid, SETUP_AP_PASSWORD);
    return true;
}

bool network_platform_start_setup_ap_background(void)
{
    return network_platform_start_setup_ap();
}

bool network_platform_stop_setup_ap(void)
{
    wifi_mode_t mode;

    if (!network_init_stack()) {
        return false;
    }
    if (!s_ap_started) {
        return true;
    }

    mode = s_sta_configured ? WIFI_MODE_STA : WIFI_MODE_NULL;
    if (esp_wifi_set_mode(mode) != ESP_OK) {
        return false;
    }

    s_ap_started = false;
    ESP_LOGI(TAG, "Setup AP stopped");
    return true;
}

bool network_platform_connect_sta(const char *ssid, const char *pass)
{
    wifi_config_t sta_cfg;
    wifi_mode_t mode = s_ap_started ? WIFI_MODE_APSTA : WIFI_MODE_STA;
    EventBits_t bits;

    if (!network_init_stack() || ssid == NULL || ssid[0] == '\0') {
        return false;
    }

    memset(&sta_cfg, 0, sizeof(sta_cfg));
    strlcpy((char *)sta_cfg.sta.ssid, ssid, sizeof(sta_cfg.sta.ssid));
    strlcpy((char *)sta_cfg.sta.password, pass ? pass : "", sizeof(sta_cfg.sta.password));
    sta_cfg.sta.scan_method = WIFI_FAST_SCAN;
    sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    if (esp_wifi_set_mode(mode) != ESP_OK) {
        return false;
    }
    if (esp_wifi_set_config(WIFI_IF_STA, &sta_cfg) != ESP_OK) {
        return false;
    }
    s_sta_configured = true;
    if (!ensure_wifi_started()) {
        return false;
    }

    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAILED_BIT);
    if (esp_wifi_connect() != ESP_OK) {
        return false;
    }

    bits = xEventGroupWaitBits(s_wifi_events,
                               WIFI_CONNECTED_BIT | WIFI_FAILED_BIT,
                               pdTRUE,
                               pdFALSE,
                               pdMS_TO_TICKS(15000));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

size_t network_platform_scan(wifi_scan_result_t *results, size_t max_results)
{
    uint16_t found = 0;
    wifi_scan_config_t scan_cfg;
    wifi_ap_record_t ap_records[16];
    size_t count;
    esp_err_t err;

    if (!network_init_stack() || results == NULL || max_results == 0) {
        return 0;
    }

    memset(&scan_cfg, 0, sizeof(scan_cfg));
    memset(ap_records, 0, sizeof(ap_records));
    if (!ensure_wifi_started()) {
        return 0;
    }
    s_scanning = true;
    err = esp_wifi_scan_start(&scan_cfg, true);
    s_scanning = false;
    if (err != ESP_OK) {
        return 0;
    }
    found = (uint16_t)(sizeof(ap_records) / sizeof(ap_records[0]));
    if (esp_wifi_scan_get_ap_records(&found, ap_records) != ESP_OK) {
        return 0;
    }
    if (s_sta_configured && !s_sta_connected) {
        esp_wifi_connect();
    }

    count = found;
    if (count > max_results) {
        count = max_results;
    }
    if (count > (sizeof(ap_records) / sizeof(ap_records[0]))) {
        count = sizeof(ap_records) / sizeof(ap_records[0]);
    }

    for (size_t i = 0; i < count; ++i) {
        strlcpy(results[i].ssid, (const char *)ap_records[i].ssid, sizeof(results[i].ssid));
        results[i].rssi = ap_records[i].rssi;
    }

    return count;
}

bool network_platform_get_ip(char *buf, size_t buf_len)
{
    esp_netif_ip_info_t ip_info;

    if (buf == 0 || buf_len == 0) {
        return false;
    }

    buf[0] = '\0';
    if (!network_init_stack()) {
        return false;
    }

    if (s_sta_netif != NULL &&
        esp_netif_get_ip_info(s_sta_netif, &ip_info) == ESP_OK &&
        ip_info.ip.addr != 0) {
        snprintf(buf, buf_len, IPSTR, IP2STR(&ip_info.ip));
        return true;
    }

    if (s_ap_netif != NULL &&
        esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK &&
        ip_info.ip.addr != 0) {
        snprintf(buf, buf_len, IPSTR, IP2STR(&ip_info.ip));
        return true;
    }

    return false;
}
