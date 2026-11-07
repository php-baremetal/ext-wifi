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
 *
 * Contract 1.1 adds events, a non-blocking connect with automatic retries, an asynchronous scan and a few
 * queries (the block at the end); every function of 1.0 stays and behaves as before.
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

/* ---- since wifi-hal 1.1 ---- */
typedef enum {
    BM_WIFI_EV_STA_CONNECTED = 1,   /* associated (no IP yet) */
    BM_WIFI_EV_STA_DISCONNECTED,    /* an association ended: reason, code, retrying (reason BY_USER = asked for) */
    BM_WIFI_EV_STA_GOT_IP,          /* ip */
    BM_WIFI_EV_STA_LOST_IP,         /* the IP went while still associated (not raised by a disconnect) */
    BM_WIFI_EV_STA_GAVE_UP,         /* a connect gave up: reason, code (no retries left, or none allowed) */
    BM_WIFI_EV_SCAN_DONE,           /* bm_wifi_hal_scan_results() has the list */
    BM_WIFI_EV_AP_STARTED,
    BM_WIFI_EV_AP_STOPPED,          /* clients: how many were connected */
    BM_WIFI_EV_AP_CLIENT_JOINED,    /* mac, clients (after the change) */
    BM_WIFI_EV_AP_CLIENT_LEFT,      /* mac, clients */
} bm_wifi_ev_type_t;
typedef enum {
    BM_WIFI_REASON_OTHER = 0, BM_WIFI_REASON_WRONG_PASSWORD, BM_WIFI_REASON_NOT_FOUND,
    BM_WIFI_REASON_LOST, BM_WIFI_REASON_BY_USER,
} bm_wifi_reason_t;
/* Fields valid per type (the others are zero): STA_DISCONNECTED reason, code, retrying; STA_GOT_IP ip;
 * STA_GAVE_UP reason, code; AP_STOPPED clients; AP_CLIENT_JOINED/LEFT mac, clients (after the change);
 * STA_CONNECTED, STA_LOST_IP, SCAN_DONE, AP_STARTED none (use sta_ssid() / ap_ip()). */
typedef struct {
    bm_wifi_ev_type_t type;
    bm_wifi_reason_t reason;
    int     code;          /* the platform's raw reason code, 0 when none */
    bool    retrying;      /* STA_DISCONNECTED: the HAL reconnects by itself */
    uint8_t ip[4];
    uint8_t mac[6];
    int     clients;
} bm_wifi_hal_event_t;
/* One listener (NULL removes it). Called from one platform task, one event at a time, in the order the
 * platform raises them; never from an interrupt; it must not block for long (a short task-level lock is fine). */
void bm_wifi_hal_set_listener(void (*cb)(const bm_wifi_hal_event_t *ev));
/* As bm_wifi_hal_sta_connect(), plus: wait false starts and returns at once (true when started; the outcome
 * comes as STA_GOT_IP or STA_GAVE_UP); events fire the same way for wait true and false.
 * wait true: when timeout_ms passes without an IP the HAL stops trying and emits STA_GAVE_UP (reason OTHER,
 * code 0), then returns false. wait false: timeout_ms is ignored, only `retries` bounds the attempts.
 * retries = automatic retries after a failed or dropped association, the initial association included (the
 * first attempt itself is not counted), refilled at every IP (0 none, -1 forever). With none left and the
 * station still wanting to connect: STA_DISCONNECTED (retrying false) if it was associated, then STA_GAVE_UP.
 * Exception: two password refusals in a row before the attempt's first IP give up at once with WRONG_PASSWORD,
 * whatever `retries` is (-1 included). A single refusal is retried like any other reason (with no retry left
 * the attempt gives up with WRONG_PASSWORD); any other reason starts the count again. Every other reason,
 * NOT_FOUND included, uses the retries.
 * Reasons: before the first IP a password refusal is WRONG_PASSWORD; once associated, every other end that
 * was not asked for is LOST; before the association NOT_FOUND or OTHER (LOST after an IP, while rejoining).
 * A new connect that cancels one in progress, like sta_disconnect(): STA_DISCONNECTED with reason BY_USER
 * (retrying false) only if it was associated, nothing if still associating, never STA_GAVE_UP.
 * After a wanted disconnect (sta_disconnect(), or a connect that cancels one in progress) the HAL does not
 * deliver an STA_CONNECTED or STA_GOT_IP still pending for the old attempt.
 * wait true and success: the listener has been called for STA_GOT_IP before this returns true. */
bool bm_wifi_hal_sta_connect_ex(const char *ssid, const char *password, int timeout_ms, bool wait, int retries);
/* Starts a scan and returns (false when it cannot start, or one is running); SCAN_DONE fires once for every
 * started scan when it ends, success or not; after a failed scan scan_results() returns -1. */
bool bm_wifi_hal_scan_start(void);
/* The list of the last finished scan, as bm_wifi_hal_scan(); -1 when there is none. */
int bm_wifi_hal_scan_results(bm_wifi_ap_t *out, int max);
/* The SSID asked for or in use ("" when none), into out (>= 33 bytes). It stays after STA_GAVE_UP, until the
 * next connect or sta_disconnect() (ConnectFailed reads it when the event is delivered). */
void bm_wifi_hal_sta_ssid(char *out, size_t len);
bool bm_wifi_hal_ap_active(void);
