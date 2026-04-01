#ifndef WIFI_SETUP_SERVICE_H
#define WIFI_SETUP_SERVICE_H

#include <stdbool.h>
#include <stddef.h>

#include "network_platform.h"

bool wifi_setup_service_init(void);
size_t wifi_setup_service_scan(wifi_scan_result_t *results, size_t max_results);
bool wifi_setup_service_save_credentials(const char *ssid, const char *pass);
bool wifi_setup_service_get_saved_password(char *buf, size_t buf_len);
bool wifi_setup_service_get_saved_ssid(char *buf, size_t buf_len);

#endif
