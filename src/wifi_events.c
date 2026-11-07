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
 * The events of ext-wifi on the event bus: Baremetal\Wifi\Event and its nine final classes, and wifi_events(),
 * which returns a Baremetal\Wifi\Events (a Baremetal\Source).
 *
 * The first wifi_events() takes one source tag per class (no coalesce) and a filler for each, then sets
 * wifi_state_hook: from then on every HAL event, and every state change made by a PHP function, raises its
 * events with bm_events_emit_data() from the task it comes from (the HAL's or PHP's). The record (the tags and
 * whether the emission is on) is static, so it lives until the module goes; every call returns a new handle on
 * it. stop()/start() only decide whether events are emitted: the state is kept up to date either way.
 *
 * What travels in the slot is a few bytes per class (the states, the reason and code, an IP, a MAC and the
 * clients); the SSIDs, the access point IP and the scan list are read on the PHP task when the event is
 * delivered.
 */
#include "php.h"
#include "zend_exceptions.h"
#include "event_bus_php.h"
#include "wifi_state.h"

#include <stdatomic.h>
#include <string.h>

enum { EV_STATE, EV_ONLINE, EV_FAILED, EV_DISCONNECTED, EV_SCAN, EV_AP_STARTED, EV_AP_STOPPED, EV_JOINED, EV_LEFT,
       EV_N };

static const char *const s_names[EV_N] = {
    [EV_STATE] = "StateChanged", [EV_ONLINE] = "Online", [EV_FAILED] = "ConnectFailed",
    [EV_DISCONNECTED] = "Disconnected", [EV_SCAN] = "ScanDone", [EV_AP_STARTED] = "ApStarted",
    [EV_AP_STOPPED] = "ApStopped", [EV_JOINED] = "ClientJoined", [EV_LEFT] = "ClientLeft",
};

static zend_class_entry *s_ce[EV_N], *s_event_ce, *s_events_ce;

/* The source: written once by the first wifi_events() (PHP task), before the hook is set; `on` is read by any
   task. */
static struct {
    uint16_t tags[EV_N];
    bool ready;
    atomic_bool on;
} s_rec;

/* The SSID given to the last wifi_ap_start() (ApStarted::$ssid; the HAL has no query for it). PHP task only. */
static char s_ap_ssid[33];

/* ---- what each event carries (at most BM_EVENTS_DATA_MAX bytes) ---- */

typedef struct { uint8_t state, prev; } ev_state_t;
typedef struct { uint8_t ip[4]; } ev_online_t;
typedef struct { uint8_t reason; int16_t code; } ev_failed_t;
typedef struct { uint8_t reason; uint8_t retrying; int16_t code; } ev_disconnected_t;
typedef struct { uint8_t clients; } ev_clients_t;
typedef struct { uint8_t mac[6]; uint8_t clients; } ev_client_t;

_Static_assert(sizeof(ev_state_t) <= BM_EVENTS_DATA_MAX, "StateChanged data");
_Static_assert(sizeof(ev_online_t) <= BM_EVENTS_DATA_MAX, "Online data");
_Static_assert(sizeof(ev_failed_t) <= BM_EVENTS_DATA_MAX, "ConnectFailed data");
_Static_assert(sizeof(ev_disconnected_t) <= BM_EVENTS_DATA_MAX, "Disconnected data");
_Static_assert(sizeof(ev_clients_t) <= BM_EVENTS_DATA_MAX, "ApStopped data");
_Static_assert(sizeof(ev_client_t) <= BM_EVENTS_DATA_MAX, "ClientJoined/ClientLeft data");

static int16_t code16(int code)
{
    return (int16_t) (code > 32767 ? 32767 : code < -32767 ? -32767 : code);
}

static uint8_t clients8(int n)
{
    return (uint8_t) (n < 0 ? 0 : n > 255 ? 255 : n);
}

static void emit(int which, const void *data, size_t len)
{
    bm_events_emit_data(s_rec.tags[which], NULL, data, len);   /* dropped and counted when no slot is free */
}

/* wifi_state_hook: any task (the HAL's, or PHP's for a change made by a PHP function). */
static void wifi_events_hook(const bm_wifi_hal_event_t *ev, wifi_st_t prev, wifi_st_t now, bool changed)
{
    if (!atomic_load(&s_rec.on)) {
        return;
    }
    if (changed) {
        ev_state_t d = { (uint8_t) now, (uint8_t) prev };
        emit(EV_STATE, &d, sizeof d);
    }
    if (ev == NULL) {
        return;
    }
    switch (ev->type) {
    case BM_WIFI_EV_STA_GOT_IP: {
        ev_online_t d;
        memcpy(d.ip, ev->ip, sizeof d.ip);
        emit(EV_ONLINE, &d, sizeof d);
        break;
    }
    case BM_WIFI_EV_STA_GAVE_UP: {
        ev_failed_t d = { (uint8_t) ev->reason, code16(ev->code) };
        emit(EV_FAILED, &d, sizeof d);
        break;
    }
    case BM_WIFI_EV_STA_DISCONNECTED: {
        ev_disconnected_t d = { (uint8_t) ev->reason, ev->retrying ? 1 : 0, code16(ev->code) };
        emit(EV_DISCONNECTED, &d, sizeof d);
        break;
    }
    case BM_WIFI_EV_SCAN_DONE:
        emit(EV_SCAN, NULL, 0);
        break;
    case BM_WIFI_EV_AP_STARTED:
        emit(EV_AP_STARTED, NULL, 0);
        break;
    case BM_WIFI_EV_AP_STOPPED: {
        ev_clients_t d = { clients8(ev->clients) };
        emit(EV_AP_STOPPED, &d, sizeof d);
        break;
    }
    case BM_WIFI_EV_AP_CLIENT_JOINED:
    case BM_WIFI_EV_AP_CLIENT_LEFT: {
        ev_client_t d;
        memcpy(d.mac, ev->mac, sizeof d.mac);
        d.clients = clients8(ev->clients);
        emit(ev->type == BM_WIFI_EV_AP_CLIENT_JOINED ? EV_JOINED : EV_LEFT, &d, sizeof d);
        break;
    }
    default:   /* STA_CONNECTED, STA_LOST_IP: only their StateChanged */
        break;
    }
}

/* ---- fillers (PHP task, at delivery) ---- */

/* The slot data into `out` (the slot is only 4-byte aligned on the board: copied, never cast). */
static bool slot_read(const evt_t *slot, void *out, size_t len)
{
    size_t n;
    const void *p = bm_events_slot_data(slot, &n);
    if (p == NULL || n != len) {
        return false;
    }
    memcpy(out, p, len);
    return true;
}

static void set_obj(zend_object *event, zend_class_entry *ce, const char *name, zend_object *obj)
{
    zval z;
    ZVAL_OBJ(&z, obj);   /* borrowed: the property takes its own reference */
    bm_events_set_zval(event, ce, name, &z);
}

static void set_str(zend_object *event, zend_class_entry *ce, const char *name, const char *s)
{
    zval z;
    ZVAL_STRING(&z, s);
    bm_events_set_zval(event, ce, name, &z);
    zval_ptr_dtor(&z);
}

static void sta_ssid(char out[33])
{
    bm_wifi_hal_sta_ssid(out, 33);
    out[32] = '\0';
}

static void fill_state(zend_object *event, zend_object *device, const evt_t *slot)
{
    (void) device;
    ev_state_t d;
    if (!slot_read(slot, &d, sizeof d)) {
        return;
    }
    zend_class_entry *ce = s_ce[EV_STATE];
    set_obj(event, ce, "state", wifi_state_case((wifi_st_t) d.state));
    set_obj(event, ce, "previous", wifi_state_case((wifi_st_t) d.prev));
    char ssid[33] = "";
    if (d.state != WIFI_ST_IDLE) {
        sta_ssid(ssid);
    }
    if (ssid[0] == '\0') {
        zval z;
        ZVAL_NULL(&z);
        bm_events_set_zval(event, ce, "ssid", &z);
    } else {
        set_str(event, ce, "ssid", ssid);
    }
}

static void fill_online(zend_object *event, zend_object *device, const evt_t *slot)
{
    (void) device;
    ev_online_t d;
    if (!slot_read(slot, &d, sizeof d)) {
        return;
    }
    char ip[16], ssid[33];
    snprintf(ip, sizeof ip, "%u.%u.%u.%u", (unsigned) d.ip[0], (unsigned) d.ip[1], (unsigned) d.ip[2],
             (unsigned) d.ip[3]);
    sta_ssid(ssid);
    set_str(event, s_ce[EV_ONLINE], "ip", ip);
    set_str(event, s_ce[EV_ONLINE], "ssid", ssid);
}

static void fill_failed(zend_object *event, zend_object *device, const evt_t *slot)
{
    (void) device;
    ev_failed_t d;
    if (!slot_read(slot, &d, sizeof d)) {
        return;
    }
    char ssid[33];
    sta_ssid(ssid);
    zend_class_entry *ce = s_ce[EV_FAILED];
    set_obj(event, ce, "reason", wifi_reason_case((bm_wifi_reason_t) d.reason));
    bm_events_set_long(event, ce, "code", d.code);
    set_str(event, ce, "ssid", ssid);
}

static void fill_disconnected(zend_object *event, zend_object *device, const evt_t *slot)
{
    (void) device;
    ev_disconnected_t d;
    if (!slot_read(slot, &d, sizeof d)) {
        return;
    }
    zend_class_entry *ce = s_ce[EV_DISCONNECTED];
    set_obj(event, ce, "reason", wifi_reason_case((bm_wifi_reason_t) d.reason));
    bm_events_set_long(event, ce, "code", d.code);
    bm_events_set_bool(event, ce, "retrying", d.retrying != 0);
}

/* The list of the last finished scan, read now (a scan started meanwhile shows its own results); [] when there
   is none (the scan failed). */
static void fill_scan(zend_object *event, zend_object *device, const evt_t *slot)
{
    (void) device;
    (void) slot;
    bm_wifi_ap_t *aps = emalloc(WIFI_SCAN_MAX * sizeof(bm_wifi_ap_t));
    int n = bm_wifi_hal_scan_results(aps, WIFI_SCAN_MAX);
    zval list;
    array_init(&list);
    if (n > 0) {
        wifi_scan_rows(&list, aps, n > WIFI_SCAN_MAX ? WIFI_SCAN_MAX : n);
    }
    efree(aps);
    bm_events_set_zval(event, s_ce[EV_SCAN], "networks", &list);
    zval_ptr_dtor(&list);
}

static void fill_ap_started(zend_object *event, zend_object *device, const evt_t *slot)
{
    (void) device;
    (void) slot;
    char ip[16];
    bm_wifi_hal_ap_ip(ip, sizeof ip);
    ip[sizeof ip - 1] = '\0';
    set_str(event, s_ce[EV_AP_STARTED], "ssid", s_ap_ssid);
    set_str(event, s_ce[EV_AP_STARTED], "ip", ip);
}

static void fill_ap_stopped(zend_object *event, zend_object *device, const evt_t *slot)
{
    (void) device;
    ev_clients_t d;
    if (slot_read(slot, &d, sizeof d)) {
        bm_events_set_long(event, s_ce[EV_AP_STOPPED], "clients", d.clients);
    }
}

static void fill_client(zend_object *event, zend_class_entry *ce, const evt_t *slot)
{
    ev_client_t d;
    if (!slot_read(slot, &d, sizeof d)) {
        return;
    }
    char mac[18];
    snprintf(mac, sizeof mac, "%02x:%02x:%02x:%02x:%02x:%02x",
             d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5]);
    set_str(event, ce, "mac", mac);
    bm_events_set_long(event, ce, "clients", d.clients);
}

static void fill_joined(zend_object *event, zend_object *device, const evt_t *slot)
{
    (void) device;
    fill_client(event, s_ce[EV_JOINED], slot);
}

static void fill_left(zend_object *event, zend_object *device, const evt_t *slot)
{
    (void) device;
    fill_client(event, s_ce[EV_LEFT], slot);
}

static const bm_event_filler_t s_fillers[EV_N] = {
    [EV_STATE] = fill_state, [EV_ONLINE] = fill_online, [EV_FAILED] = fill_failed,
    [EV_DISCONNECTED] = fill_disconnected, [EV_SCAN] = fill_scan, [EV_AP_STARTED] = fill_ap_started,
    [EV_AP_STOPPED] = fill_ap_stopped, [EV_JOINED] = fill_joined, [EV_LEFT] = fill_left,
};

/* ---- the source and its handle ---- */

static void src_stop(void *rec)
{
    (void) rec;
    atomic_store(&s_rec.on, false);
}

static void src_start(void *rec)
{
    (void) rec;
    atomic_store(&s_rec.on, true);
}

static bool src_running(void *rec)
{
    (void) rec;
    return atomic_load(&s_rec.on);
}

static const bm_source_ops_t s_ops = { src_stop, src_start, src_running, NULL };

/* wifi_events(): Baremetal\Wifi\Events -- registers the classes (first call) and turns the emission on. */
PHP_FUNCTION(wifi_events)
{
    ZEND_PARSE_PARAMETERS_NONE();
    if (s_events_ce == NULL) {
        zend_throw_error(NULL, "wifi_events(): needs ext-events loaded before ext-wifi");
        RETURN_THROWS();
    }
    if (!s_rec.ready) {
        uint16_t tags[EV_N];
        for (int i = 0; i < EV_N; i++) {
            if (!bm_events_source_tag(s_ce[i]->name, &tags[i])) {
                if (!EG(exception)) {
                    zend_throw_exception_ex(zend_ce_exception, 0,
                        "wifi_events(): unknown class '%s' or too many sources", ZSTR_VAL(s_ce[i]->name));
                }
                RETURN_THROWS();   /* the tags taken so far are found again by the next call */
            }
        }
        for (int i = 0; i < EV_N; i++) {
            s_rec.tags[i] = tags[i];
            bm_events_set_filler(tags[i], s_fillers[i]);
        }
        s_rec.ready = true;
        atomic_store(&s_rec.on, true);
        __atomic_store_n(&wifi_state_hook, wifi_events_hook, __ATOMIC_RELEASE);   /* after the tags */
    } else {
        atomic_store(&s_rec.on, true);
    }
    zend_object *h = bm_events_source_new(s_events_ce, &s_ops, &s_rec);
    if (h == NULL) {
        RETURN_THROWS();
    }
    RETURN_OBJ(h);
}

void wifi_events_ap_ssid(const char *ssid)
{
    snprintf(s_ap_ssid, sizeof s_ap_ssid, "%s", ssid != NULL ? ssid : "");
}

/* ---- classes ---- */

static void declare(zend_class_entry *ce, const char *name, zend_type type)
{
    zval undef;
    ZVAL_UNDEF(&undef);
    zend_declare_typed_property(ce, zend_string_init_interned(name, strlen(name), 1), &undef,
                                ZEND_ACC_PUBLIC | ZEND_ACC_READONLY, NULL, type);
}

static zend_type class_type(zend_class_entry *ce)
{
    return (zend_type) ZEND_TYPE_INIT_CLASS(ce->name, 0, 0);   /* an internal class: its name is interned */
}

zend_result wifi_events_minit(void)
{
    zend_class_entry *base = bm_events_base_ce(), *source = bm_events_source_ce();
    if (base == NULL || source == NULL) {
        return SUCCESS;   /* wifi_events() throws; the synchronous functions still work */
    }
    zend_class_entry ce;
    INIT_NS_CLASS_ENTRY(ce, "Baremetal\\Wifi", "Event", NULL);
    s_event_ce = zend_register_internal_class_ex(&ce, base);
    s_event_ce->ce_flags |= ZEND_ACC_EXPLICIT_ABSTRACT_CLASS;
    for (int i = 0; i < EV_N; i++) {
        char full[64];
        snprintf(full, sizeof full, "Baremetal\\Wifi\\%s", s_names[i]);
        INIT_CLASS_ENTRY_EX(ce, full, strlen(full), NULL);
        s_ce[i] = zend_register_internal_class_ex(&ce, s_event_ce);
        s_ce[i]->ce_flags |= ZEND_ACC_FINAL;
    }
    zend_type state = class_type(wifi_state_enum_ce()), reason = class_type(wifi_reason_enum_ce());
    zend_type str = (zend_type) ZEND_TYPE_INIT_CODE(IS_STRING, 0, 0);
    zend_type nstr = (zend_type) ZEND_TYPE_INIT_CODE(IS_STRING, 1, 0);
    zend_type lng = (zend_type) ZEND_TYPE_INIT_CODE(IS_LONG, 0, 0);
    zend_type bln = (zend_type) ZEND_TYPE_INIT_CODE(_IS_BOOL, 0, 0);
    zend_type arr = (zend_type) ZEND_TYPE_INIT_CODE(IS_ARRAY, 0, 0);

    declare(s_ce[EV_STATE], "state", state);
    declare(s_ce[EV_STATE], "previous", state);
    declare(s_ce[EV_STATE], "ssid", nstr);
    declare(s_ce[EV_ONLINE], "ip", str);
    declare(s_ce[EV_ONLINE], "ssid", str);
    declare(s_ce[EV_FAILED], "reason", reason);
    declare(s_ce[EV_FAILED], "code", lng);
    declare(s_ce[EV_FAILED], "ssid", str);
    declare(s_ce[EV_DISCONNECTED], "reason", reason);
    declare(s_ce[EV_DISCONNECTED], "code", lng);
    declare(s_ce[EV_DISCONNECTED], "retrying", bln);
    declare(s_ce[EV_SCAN], "networks", arr);
    declare(s_ce[EV_AP_STARTED], "ssid", str);
    declare(s_ce[EV_AP_STARTED], "ip", str);
    declare(s_ce[EV_AP_STOPPED], "clients", lng);
    declare(s_ce[EV_JOINED], "mac", str);
    declare(s_ce[EV_JOINED], "clients", lng);
    declare(s_ce[EV_LEFT], "mac", str);
    declare(s_ce[EV_LEFT], "clients", lng);

    INIT_NS_CLASS_ENTRY(ce, "Baremetal\\Wifi", "Events", NULL);
    s_events_ce = zend_register_internal_class_ex(&ce, source);   /* create_object comes from Source */
    s_events_ce->ce_flags |= ZEND_ACC_FINAL | ZEND_ACC_NOT_SERIALIZABLE;
    return SUCCESS;
}

void wifi_events_mshutdown(void)
{
    __atomic_store_n(&wifi_state_hook, NULL, __ATOMIC_RELEASE);
    atomic_store(&s_rec.on, false);
}
