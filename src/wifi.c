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
 * wifi: scan/join a WiFi network (STA) and create one (SoftAP) from PHP.
 *
 * Available only on SoCs with a WiFi radio (ESP32 / ESP32-S3 / C-series), not the ESP32-P4.
 *
 * Neutral PHP binding: it talks only to the WiFi HAL (bm_wifi_hal.h); the arch backing lives in
 * bm_wifi_hal_<arch>.c.
 *
 * PHP API:
 *   wifi_scan(bool $wait=true): array|bool               -- [{ssid,bssid,rssi,channel,auth}, ...]; wait false:
 *                                                           starts a scan and returns whether it started
 *   wifi_connect(string $ssid, ?string $password=null, int $timeout_ms=30000, bool $wait=true, int $retries=10): bool
 *   wifi_disconnect(): bool
 *   wifi_connected(): bool
 *   wifi_state(): Baremetal\Wifi\State   wifi_ssid(): ?string   wifi_ap_active(): bool
 *   wifi_ip(): ?string       wifi_rssi(): ?int
 *   wifi_ap_start(string $ssid, ?string $password=null, int $channel=1, int $max_conn=4): bool
 *   wifi_ap_stop(): bool
 *   wifi_ap_ip(): ?string    wifi_ap_clients(): int
 *   wifi_available(): bool   -- whether the radio comes up
 *   wifi_events(): Baremetal\Wifi\Events -- the events on the event bus (wifi_events.c)
 */
#include "php.h"
#include "php_wifi.h"
#include "bm_wifi_hal.h"
#include "wifi_state.h"
#include <limits.h>
#include <string.h>

#define WIFI_SSID_MAX 32   /* 802.11 */
#define WIFI_PASS_MAX 64   /* a WPA2 passphrase is 8 to 63 characters, a raw PSK 64 hex digits */

/* An SSID of 1 to 32 bytes and a password of at most 64, neither with a NUL byte (the HAL would cut
 * there, and a password starting with NUL would open the network); false (with a ValueError) otherwise. */
static bool wifi_check_credentials(const char *ssid, size_t ssid_len, const char *pw, size_t pw_len)
{
    if (ssid_len < 1 || ssid_len > WIFI_SSID_MAX) {
        zend_argument_value_error(1, "must be 1 to 32 bytes long");
        return false;
    }
    if (memchr(ssid, '\0', ssid_len)) {
        zend_argument_value_error(1, "must not contain any null bytes");
        return false;
    }
    if (pw != NULL && pw_len > WIFI_PASS_MAX) {
        zend_argument_value_error(2, "must be at most 64 bytes long");
        return false;
    }
    if (pw != NULL && memchr(pw, '\0', pw_len)) {
        zend_argument_value_error(2, "must not contain any null bytes");
        return false;
    }
    return true;
}

/* No-argument functions, one arginfo per return type. */
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_wifi_bool, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

/* wifi_scan() returns the AP list, or false on a scan error; with wait false whether the scan started. */
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_MASK_EX(arginfo_wifi_scan, 0, 0, MAY_BE_ARRAY|MAY_BE_BOOL)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, wait, _IS_BOOL, 0, "true")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_wifi_state, 0, 0, Baremetal\\Wifi\\State, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_wifi_events, 0, 0, Baremetal\\Wifi\\Events, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_wifi_string, 0, 0, IS_STRING, 1)   /* ?string */
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_wifi_int_nullable, 0, 0, IS_LONG, 1)   /* ?int */
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_wifi_int, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_wifi_connect, 0, 1, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, ssid, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, password, IS_STRING, 1, "null")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, timeout_ms, IS_LONG, 0, "30000")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, wait, _IS_BOOL, 0, "true")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, retries, IS_LONG, 0, "10")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_wifi_ap_start, 0, 1, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, ssid, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, password, IS_STRING, 1, "null")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, channel, IS_LONG, 0, "1")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, max_conn, IS_LONG, 0, "4")
ZEND_END_ARG_INFO()

/* Whether the radio comes up: false when it cannot (a Wi-Fi companion chip that did not start, for example). */
PHP_FUNCTION(wifi_available)
{
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_BOOL(bm_wifi_hal_init() == 0);
}

/* A state change made by a PHP function: wifi_state_set() calls the hook (ev NULL) when the state moved. */
static void wifi_state_move(wifi_st_t st)
{
    bool changed;
    (void) wifi_state_set(st, &changed);
}

void wifi_scan_rows(zval *out, const bm_wifi_ap_t *aps, int n)
{
    for (int i = 0; i < n; i++) {
        zval row;
        array_init(&row);
        add_assoc_string(&row, "ssid", aps[i].ssid);
        char bssid[18];
        snprintf(bssid, sizeof bssid, "%02x:%02x:%02x:%02x:%02x:%02x",
                 aps[i].bssid[0], aps[i].bssid[1], aps[i].bssid[2],
                 aps[i].bssid[3], aps[i].bssid[4], aps[i].bssid[5]);
        add_assoc_string(&row, "bssid", bssid);
        add_assoc_long(&row, "rssi", aps[i].rssi);
        add_assoc_long(&row, "channel", aps[i].channel);
        add_assoc_string(&row, "auth", aps[i].auth);
        add_next_index_zval(out, &row);
    }
}

PHP_FUNCTION(wifi_scan)
{
    bool wait = true;

    ZEND_PARSE_PARAMETERS_START(0, 1)
        Z_PARAM_OPTIONAL
        Z_PARAM_BOOL(wait)
    ZEND_PARSE_PARAMETERS_END();

    if (bm_wifi_hal_init() != 0) {
        RETURN_FALSE;
    }
    if (!wait) {
        RETURN_BOOL(bm_wifi_hal_scan_start());
    }
    bm_wifi_ap_t *aps = emalloc(WIFI_SCAN_MAX * sizeof(bm_wifi_ap_t));
    int n = bm_wifi_hal_scan(aps, WIFI_SCAN_MAX);
    if (n < 0) {
        efree(aps);
        RETURN_FALSE;
    }
    array_init(return_value);
    wifi_scan_rows(return_value, aps, n);
    efree(aps);
}

PHP_FUNCTION(wifi_connect)
{
    char *ssid = NULL, *pw = NULL;
    size_t ssid_len = 0, pw_len = 0;
    zend_long timeout = 30000, retries = 10;
    bool wait = true;

    ZEND_PARSE_PARAMETERS_START(1, 5)
        Z_PARAM_STRING(ssid, ssid_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING_OR_NULL(pw, pw_len)
        Z_PARAM_LONG(timeout)
        Z_PARAM_BOOL(wait)
        Z_PARAM_LONG(retries)
    ZEND_PARSE_PARAMETERS_END();

    if (!wifi_check_credentials(ssid, ssid_len, pw, pw_len)) {
        RETURN_THROWS();
    }
    if (retries < -1) {
        zend_value_error("wifi_connect(): $retries must be -1 (forever) or more");
        RETURN_THROWS();
    }
#if SIZEOF_ZEND_LONG > 4
    if (retries > INT_MAX) {
        retries = INT_MAX;   /* the HAL counts in an int: as good as forever */
    }
    if (timeout > INT_MAX) {
        timeout = INT_MAX;
    }
#endif
    if (bm_wifi_hal_init() != 0) {
        RETURN_FALSE;
    }
    if (timeout <= 0) {
        timeout = 30000;
    }
    /* Connecting before the HAL starts, so its first events find it; a connect that did not reach an IP
       (wait true) or did not start leaves Connecting for Idle, unless an event (STA_GAVE_UP) already moved it. */
    wifi_state_move(WIFI_ST_CONNECTING);
    bool ok = bm_wifi_hal_sta_connect_ex(ssid, pw, (int) timeout, wait, (int) retries);
    if (!ok) {
        bool changed;
        (void) wifi_state_set_if(WIFI_ST_CONNECTING, WIFI_ST_IDLE, &changed);
    }
    RETURN_BOOL(ok);
}

/* Closes the station or cancels a connect in progress and its retries (the HAL raises STA_DISCONNECTED BY_USER
   if it was associated); the state goes to Idle. */
PHP_FUNCTION(wifi_disconnect)
{
    ZEND_PARSE_PARAMETERS_NONE();
    bm_wifi_hal_sta_disconnect();
    wifi_state_move(WIFI_ST_IDLE);
    RETURN_TRUE;
}

PHP_FUNCTION(wifi_state)
{
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_OBJ_COPY(wifi_state_case(wifi_state_get()));
}

/* The network asked for or in use; null in Idle or when the HAL has none. */
PHP_FUNCTION(wifi_ssid)
{
    ZEND_PARSE_PARAMETERS_NONE();
    if (wifi_state_get() == WIFI_ST_IDLE) {
        RETURN_NULL();
    }
    char ssid[WIFI_SSID_MAX + 1];
    bm_wifi_hal_sta_ssid(ssid, sizeof ssid);
    if (ssid[0] == '\0') {
        RETURN_NULL();
    }
    RETURN_STRING(ssid);
}

PHP_FUNCTION(wifi_ap_active)
{
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_BOOL(bm_wifi_hal_ap_active());
}

PHP_FUNCTION(wifi_connected)
{
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_BOOL(bm_wifi_hal_sta_connected());
}

PHP_FUNCTION(wifi_ip)
{
    ZEND_PARSE_PARAMETERS_NONE();
    char ip[16];
    bm_wifi_hal_sta_ip(ip, sizeof ip);
    if (ip[0] == '\0') {
        RETURN_NULL();
    }
    RETURN_STRING(ip);
}

PHP_FUNCTION(wifi_rssi)
{
    ZEND_PARSE_PARAMETERS_NONE();
    int rssi;
    if (!bm_wifi_hal_sta_rssi(&rssi)) {
        RETURN_NULL();
    }
    RETURN_LONG(rssi);
}

PHP_FUNCTION(wifi_ap_start)
{
    char *ssid = NULL, *pw = NULL;
    size_t ssid_len = 0, pw_len = 0;
    zend_long channel = 1, max_conn = 4;

    ZEND_PARSE_PARAMETERS_START(1, 4)
        Z_PARAM_STRING(ssid, ssid_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING_OR_NULL(pw, pw_len)
        Z_PARAM_LONG(channel)
        Z_PARAM_LONG(max_conn)
    ZEND_PARSE_PARAMETERS_END();

    if (!wifi_check_credentials(ssid, ssid_len, pw, pw_len)) {
        RETURN_THROWS();
    }
    if (bm_wifi_hal_init() != 0) {
        RETURN_FALSE;
    }
    bool ok = bm_wifi_hal_ap_start(ssid, pw, (int) channel, (int) max_conn);
    if (ok) {
        wifi_events_ap_ssid(ssid);   /* ApStarted is filled later, on this task: a refused SSID never shows */
    }
    RETURN_BOOL(ok);
}

PHP_FUNCTION(wifi_ap_stop)
{
    ZEND_PARSE_PARAMETERS_NONE();
    bm_wifi_hal_ap_stop();
    RETURN_TRUE;
}

PHP_FUNCTION(wifi_ap_ip)
{
    ZEND_PARSE_PARAMETERS_NONE();
    char ip[16];
    bm_wifi_hal_ap_ip(ip, sizeof ip);
    if (ip[0] == '\0') {
        RETURN_NULL();
    }
    RETURN_STRING(ip);
}

PHP_FUNCTION(wifi_ap_clients)
{
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_LONG(bm_wifi_hal_ap_clients());
}

static const zend_function_entry wifi_functions[] = {
    PHP_FE(wifi_available,   arginfo_wifi_bool)
    PHP_FE(wifi_scan,        arginfo_wifi_scan)
    PHP_FE(wifi_connect,     arginfo_wifi_connect)
    PHP_FE(wifi_disconnect,  arginfo_wifi_bool)
    PHP_FE(wifi_connected,   arginfo_wifi_bool)
    PHP_FE(wifi_ip,          arginfo_wifi_string)
    PHP_FE(wifi_rssi,        arginfo_wifi_int_nullable)
    PHP_FE(wifi_ap_start,    arginfo_wifi_ap_start)
    PHP_FE(wifi_ap_stop,     arginfo_wifi_bool)
    PHP_FE(wifi_ap_ip,       arginfo_wifi_string)
    PHP_FE(wifi_ap_clients,  arginfo_wifi_int)
    PHP_FE(wifi_state,       arginfo_wifi_state)
    PHP_FE(wifi_ssid,        arginfo_wifi_string)
    PHP_FE(wifi_ap_active,   arginfo_wifi_bool)
    PHP_FE(wifi_events,      arginfo_wifi_events)
    PHP_FE_END
};

PHP_MINIT_FUNCTION(wifi)
{
    if (wifi_state_minit() != SUCCESS) {
        return FAILURE;
    }
    return wifi_events_minit();
}

static PHP_MSHUTDOWN_FUNCTION(wifi)
{
    wifi_events_mshutdown();
    wifi_state_mshutdown();
    return SUCCESS;
}

zend_module_entry wifi_module_entry = {
    STANDARD_MODULE_HEADER,
    "wifi",
    wifi_functions,
    PHP_MINIT(wifi),
    PHP_MSHUTDOWN(wifi),
    NULL,   /* RINIT */
    NULL,   /* RSHUTDOWN */
    NULL,   /* MINFO */
    "1.1.0",
    STANDARD_MODULE_PROPERTIES
};
