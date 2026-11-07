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
 * The station state of ext-wifi: one value under a lock, moved by the HAL events (wifi_state_on_hal(), the HAL
 * listener) and by the PHP functions (wifi_connect(), wifi_disconnect()), and the Baremetal\Wifi\State and
 * Baremetal\Wifi\Reason enums. The events (wifi_events.c) hang on wifi_state_hook.
 */
#ifndef WIFI_STATE_H
#define WIFI_STATE_H

#include "php.h"
#include "bm_wifi_hal.h"
#include <stdbool.h>

typedef enum { WIFI_ST_IDLE = 0, WIFI_ST_CONNECTING, WIFI_ST_CONNECTED, WIFI_ST_ONLINE,
               WIFI_ST_ANY = -1 /* wifi_state_set_if(): from any state */ } wifi_st_t;

/* At MINIT: creates the lock, registers the enums and gives wifi_state_on_hal() to the HAL. */
zend_result wifi_state_minit(void);
/* At MSHUTDOWN: removes the HAL listener. */
void wifi_state_mshutdown(void);

/* Moves the state and, when it changed, calls the hook (ev NULL), both under the module's gate, so the hook
   hears the changes in their order; returns the previous one, and *changed. Tasks only. */
wifi_st_t wifi_state_set(wifi_st_t st, bool *changed);
/* As wifi_state_set(), only while the state is `from` (WIFI_ST_ANY: any). */
wifi_st_t wifi_state_set_if(wifi_st_t from, wifi_st_t st, bool *changed);
wifi_st_t wifi_state_get(void);
/* The listener given to bm_wifi_hal_set_listener() at MINIT: updates the state, then calls the hook, under the
   gate (it waits for it only while a PHP function is changing the state, briefly). */
void wifi_state_on_hal(const bm_wifi_hal_event_t *ev);
/* Set by wifi_events(): called after every HAL event (state already updated) and after every state change made by
   a PHP function, outside the lock but inside the gate (one caller at a time, in the order of the changes): it
   must not block and must not change the state. NULL: nothing is emitted. A PHP-made change comes with ev NULL and changed
   true; a HAL event comes with its event, whether the state changed or not. */
extern void (*wifi_state_hook)(const bm_wifi_hal_event_t *ev, wifi_st_t prev, wifi_st_t now, bool changed);

zend_class_entry *wifi_state_enum_ce(void);    /* Baremetal\Wifi\State */
zend_class_entry *wifi_reason_enum_ce(void);   /* Baremetal\Wifi\Reason */
/* The enum case of a state or a reason (borrowed: the enum owns it). PHP thread only. */
zend_object *wifi_state_case(wifi_st_t st);
zend_object *wifi_reason_case(bm_wifi_reason_t r);

/* The rows of wifi_scan() ([ssid, bssid, rssi, channel, auth] each) for `n` access points, into `out` (an
   initialized array). In wifi.c; ScanDone reuses it. */
void wifi_scan_rows(zval *out, const bm_wifi_ap_t *aps, int n);

#define WIFI_SCAN_MAX 48   /* cap on APs returned by one scan (wifi_scan(), ScanDone) */

/* The events (wifi_events.c): classes at MINIT (after wifi_state_minit(); nothing without ext-events), the hook
   removed at MSHUTDOWN. */
zend_result wifi_events_minit(void);
void wifi_events_mshutdown(void);
/* The SSID of the access point being started, for ApStarted::$ssid. PHP task only. */
void wifi_events_ap_ssid(const char *ssid);
PHP_FUNCTION(wifi_events);

#endif /* WIFI_STATE_H */
