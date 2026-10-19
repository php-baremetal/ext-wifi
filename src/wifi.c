/*
   +----------------------------------------------------------------------+
   | php-baremetal: ext-wifi                                              |
   +----------------------------------------------------------------------+
   | Copyright (c) 2026 Gianfrancesco Aurecchia                           |
   +----------------------------------------------------------------------+
   | This source file is subject to the MIT license, that is bundled with |
   | this package in the file LICENSE.                                    |
   +----------------------------------------------------------------------+
   | Authors: Gianfrancesco Aurecchia <gianfri@aurecchia.com>             |
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
 *   wifi_scan(): array                                   -- [{ssid,bssid,rssi,channel,auth}, ...]
 *   wifi_connect(string $ssid, ?string $password=null, int $timeout_ms=15000): bool
 *   wifi_disconnect(): bool
 *   wifi_connected(): bool
 *   wifi_ip(): ?string       wifi_rssi(): ?int
 *   wifi_ap_start(string $ssid, ?string $password=null, int $channel=1, int $max_conn=4): bool
 *   wifi_ap_stop(): bool
 *   wifi_ap_ip(): ?string    wifi_ap_clients(): int
 *   wifi_available(): bool
 */
#include "php.h"
#include "php_wifi.h"
#include "bm_wifi_hal.h"
#include <string.h>

#define WIFI_SCAN_MAX 48   /* cap on APs returned by one scan */

/* No-argument functions, one arginfo per return type. */
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_wifi_bool, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

/* wifi_scan() returns the AP list, or false on a scan error. */
ZEND_BEGIN_ARG_WITH_RETURN_TYPE_MASK_EX(arginfo_wifi_scan, 0, 0, MAY_BE_ARRAY|MAY_BE_FALSE)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_wifi_string, 0, 0, IS_STRING, 1)   /* ?string */
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_wifi_int_nullable, 0, 0, IS_LONG, 1)   /* ?int */
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_wifi_int, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_wifi_connect, 0, 1, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, ssid, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, password, IS_STRING, 1)
    ZEND_ARG_TYPE_INFO(0, timeout_ms, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_wifi_ap_start, 0, 1, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, ssid, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, password, IS_STRING, 1)
    ZEND_ARG_TYPE_INFO(0, channel, IS_LONG, 0)
    ZEND_ARG_TYPE_INFO(0, max_conn, IS_LONG, 0)
ZEND_END_ARG_INFO()

PHP_FUNCTION(wifi_available)
{
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_BOOL(1);
}

PHP_FUNCTION(wifi_scan)
{
    ZEND_PARSE_PARAMETERS_NONE();
    if (bm_wifi_hal_init() != 0) {
        RETURN_FALSE;
    }
    bm_wifi_ap_t *aps = emalloc(WIFI_SCAN_MAX * sizeof(bm_wifi_ap_t));
    int n = bm_wifi_hal_scan(aps, WIFI_SCAN_MAX);
    if (n < 0) {
        efree(aps);
        RETURN_FALSE;
    }
    array_init(return_value);
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
        add_next_index_zval(return_value, &row);
    }
    efree(aps);
}

PHP_FUNCTION(wifi_connect)
{
    char *ssid = NULL, *pw = NULL;
    size_t ssid_len = 0, pw_len = 0;
    zend_long timeout = 15000;

    ZEND_PARSE_PARAMETERS_START(1, 3)
        Z_PARAM_STRING(ssid, ssid_len)
        Z_PARAM_OPTIONAL
        Z_PARAM_STRING_OR_NULL(pw, pw_len)
        Z_PARAM_LONG(timeout)
    ZEND_PARSE_PARAMETERS_END();

    if (bm_wifi_hal_init() != 0) {
        RETURN_FALSE;
    }
    if (timeout <= 0) {
        timeout = 15000;
    }
    RETURN_BOOL(bm_wifi_hal_sta_connect(ssid, pw, (int) timeout));
}

PHP_FUNCTION(wifi_disconnect)
{
    ZEND_PARSE_PARAMETERS_NONE();
    bm_wifi_hal_sta_disconnect();
    RETURN_TRUE;
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

    if (bm_wifi_hal_init() != 0) {
        RETURN_FALSE;
    }
    RETURN_BOOL(bm_wifi_hal_ap_start(ssid, pw, (int) channel, (int) max_conn));
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
    PHP_FE_END
};

PHP_MINIT_FUNCTION(wifi)
{
    return SUCCESS;
}

zend_module_entry wifi_module_entry = {
    STANDARD_MODULE_HEADER,
    "wifi",
    wifi_functions,
    PHP_MINIT(wifi),
    NULL,   /* MSHUTDOWN */
    NULL,   /* RINIT */
    NULL,   /* RSHUTDOWN */
    NULL,   /* MINFO */
    "1.0",
    STANDARD_MODULE_PROPERTIES
};
