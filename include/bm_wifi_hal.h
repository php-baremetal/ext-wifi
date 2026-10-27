/*
   +----------------------------------------------------------------------+
   | php-baremetal: ext-wifi                                              |
   +----------------------------------------------------------------------+
   | Copyright (c) 2026 Gianfrancesco Aurecchia                           |
   +----------------------------------------------------------------------+
   | This source file is subject to the MIT license, that is bundled with |
   | this package in the file LICENSE.                                    |
   +----------------------------------------------------------------------+
   | Authors: Gianfrancesco Aurecchia <gianfri@php-baremetal.com>         |
   +----------------------------------------------------------------------+
*/

/*
 * Neutral WiFi HAL: the only surface the wifi binding uses. An arch package implements the whole
 * lifecycle behind it (init, connection state, STA scan/join, SoftAP); on ESP32 that is
 * bm_wifi_hal_esp32.c over esp_wifi/esp_netif/esp_event.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* One-time bring-up of the radio (the platform owns netif and the event loop). 0 on success. Idempotent. */
int bm_wifi_hal_init(void);

/* One scanned access point. `auth` is a stable lowercase string: open/wep/wpa/wpa2/wpa3/... */
typedef struct {
    char    ssid[33];
    uint8_t bssid[6];
    int     rssi;
    int     channel;
    char    auth[16];
} bm_wifi_ap_t;
/* Scan for networks, filling up to `max` entries into `out`. Returns the count (>= 0), or -1 on error. */
int bm_wifi_hal_scan(bm_wifi_ap_t *out, int max);

/* STA: associate with `ssid`/`password` (NULL/empty = open) and block up to `timeout_ms` for an IP.
 * Returns true once connected (got IP). */
bool bm_wifi_hal_sta_connect(const char *ssid, const char *password, int timeout_ms);
void bm_wifi_hal_sta_disconnect(void);
bool bm_wifi_hal_sta_connected(void);
/* Copy the current STA IP into `out` (>= 16 bytes); "" when not connected. */
void bm_wifi_hal_sta_ip(char *out, size_t len);
/* Current STA signal into *out; false when not connected (no IP yet, or not associated). */
bool bm_wifi_hal_sta_rssi(int *out);

/* SoftAP: start/stop and query. */
bool bm_wifi_hal_ap_start(const char *ssid, const char *password, int channel, int max_conn);
void bm_wifi_hal_ap_stop(void);
/* Copy the AP IP into `out` (>= 16 bytes); "" when no AP is up. */
void bm_wifi_hal_ap_ip(char *out, size_t len);
int  bm_wifi_hal_ap_clients(void);
