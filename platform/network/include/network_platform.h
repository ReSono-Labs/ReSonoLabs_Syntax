#ifndef NETWORK_PLATFORM_H
#define NETWORK_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board_profile.h"

typedef struct {
    char ssid[64];
    int32_t rssi;
} wifi_scan_result_t;

bool network_platform_init(const board_profile_t *board);
bool network_platform_start_setup_ap(void);
bool network_platform_start_setup_ap_background(void);
bool network_platform_stop_setup_ap(void);
bool network_platform_connect_sta(const char *ssid, const char *pass);
size_t network_platform_scan(wifi_scan_result_t *results, size_t max_results);
bool network_platform_get_ip(char *buf, size_t buf_len);

#endif
