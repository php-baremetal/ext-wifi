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

/* The station state and the State/Reason enums: see wifi_state.h. */
#include "wifi_state.h"
#include "bm_events_hal.h"
#include "zend_enum.h"

static bm_hal_lock_t *s_lock;     /* the state variable, for plain reads (a critical section: never emit in it) */
static bm_hal_queue_t *s_gate;    /* a one-token queue used as a task-level lock: a state change and its hook */
static wifi_st_t s_state = WIFI_ST_IDLE;
static zend_class_entry *s_state_ce;
static zend_class_entry *s_reason_ce;

void (*wifi_state_hook)(const bm_wifi_hal_event_t *ev, wifi_st_t prev, wifi_st_t now, bool changed);

/* Case names and values, indexed by wifi_st_t and by bm_wifi_reason_t. */
static const struct { const char *name; const char *value; } s_states[] = {
    [WIFI_ST_IDLE] = { "Idle", "idle" },
    [WIFI_ST_CONNECTING] = { "Connecting", "connecting" },
    [WIFI_ST_CONNECTED] = { "Connected", "connected" },
    [WIFI_ST_ONLINE] = { "Online", "online" },
};
static const struct { const char *name; const char *value; } s_reasons[] = {
    [BM_WIFI_REASON_OTHER] = { "Other", "other" },
    [BM_WIFI_REASON_WRONG_PASSWORD] = { "WrongPassword", "wrong-password" },
    [BM_WIFI_REASON_NOT_FOUND] = { "NotFound", "not-found" },
    [BM_WIFI_REASON_LOST] = { "Lost", "lost" },
    [BM_WIFI_REASON_BY_USER] = { "ByUser", "by-user" },
};
/* Declaration order of the Reason cases (the PHP API's). */
static const bm_wifi_reason_t s_reason_order[] = {
    BM_WIFI_REASON_WRONG_PASSWORD, BM_WIFI_REASON_NOT_FOUND, BM_WIFI_REASON_LOST, BM_WIFI_REASON_BY_USER,
    BM_WIFI_REASON_OTHER,
};

static void add_case(zend_class_entry *ce, const char *name, const char *value)
{
    zval v;
    ZVAL_STR(&v, zend_string_init_interned(value, strlen(value), 1));
    zend_enum_add_case_cstr(ce, name, &v);
}

zend_result wifi_state_minit(void)
{
    s_lock = bm_hal_lock_create();
    s_gate = bm_hal_queue_create(1, 1);
    uint8_t token = 0;
    if (s_lock == NULL || s_gate == NULL || !bm_hal_queue_send(s_gate, &token, 0)) {
        return FAILURE;
    }
    s_state_ce = zend_register_internal_enum("Baremetal\\Wifi\\State", IS_STRING, NULL);
    for (size_t i = 0; i < sizeof s_states / sizeof s_states[0]; i++) {
        add_case(s_state_ce, s_states[i].name, s_states[i].value);
    }
    s_reason_ce = zend_register_internal_enum("Baremetal\\Wifi\\Reason", IS_STRING, NULL);
    for (size_t i = 0; i < sizeof s_reason_order / sizeof s_reason_order[0]; i++) {
        add_case(s_reason_ce, s_reasons[s_reason_order[i]].name, s_reasons[s_reason_order[i]].value);
    }
    bm_wifi_hal_set_listener(wifi_state_on_hal);
    return SUCCESS;
}

void wifi_state_mshutdown(void)
{
    bm_wifi_hal_set_listener(NULL);
}

/* The gate: tasks only (the HAL listener's and PHP's), never an interrupt. Held around a state change and the
   hook call, so the StateChanged events leave in the order of the changes; the hook only posts to the event
   queue (never blocks), so a holder keeps it only briefly. */
static bool gate_enter(void)
{
    uint8_t token;
    return bm_hal_queue_receive(s_gate, &token, UINT32_MAX);
}

static void gate_exit(bool entered)
{
    uint8_t token = 0;
    if (entered) {
        bm_hal_queue_send(s_gate, &token, 0);
    }
}

static void call_hook(const bm_wifi_hal_event_t *ev, wifi_st_t prev, wifi_st_t now, bool changed)
{
    void (*hook)(const bm_wifi_hal_event_t *, wifi_st_t, wifi_st_t, bool) =
        __atomic_load_n(&wifi_state_hook, __ATOMIC_ACQUIRE);
    if (hook != NULL && (ev != NULL || changed)) {
        hook(ev, prev, now, changed);
    }
}

wifi_st_t wifi_state_set(wifi_st_t st, bool *changed)
{
    return wifi_state_set_if(WIFI_ST_ANY, st, changed);
}

wifi_st_t wifi_state_set_if(wifi_st_t from, wifi_st_t st, bool *changed)
{
    bool entered = gate_enter();
    bm_hal_lock_enter(s_lock);
    wifi_st_t prev = s_state;
    bool moves = from == WIFI_ST_ANY || prev == from;
    if (moves) {
        s_state = st;
    }
    bm_hal_lock_exit(s_lock);
    *changed = moves && prev != st;
    call_hook(NULL, prev, st, *changed);
    gate_exit(entered);
    return prev;
}

wifi_st_t wifi_state_get(void)
{
    bm_hal_lock_enter(s_lock);
    wifi_st_t st = s_state;
    bm_hal_lock_exit(s_lock);
    return st;
}

void wifi_state_on_hal(const bm_wifi_hal_event_t *ev)
{
    bool entered = gate_enter();   /* waits only while a PHP function changes the state */
    bm_hal_lock_enter(s_lock);
    wifi_st_t prev = s_state, now = prev;
    /* In Idle no network is asked for: a station event is a leftover of an attempt already closed and does not
       move the state (the hook still hears it). Only wifi_connect() leaves Idle. */
    if (prev != WIFI_ST_IDLE) {
        switch (ev->type) {
        case BM_WIFI_EV_STA_CONNECTED:
            now = WIFI_ST_CONNECTED;
            break;
        case BM_WIFI_EV_STA_GOT_IP:
            now = WIFI_ST_ONLINE;
            break;
        case BM_WIFI_EV_STA_LOST_IP:
            if (prev == WIFI_ST_ONLINE) {
                now = WIFI_ST_CONNECTED;
            }
            break;
        case BM_WIFI_EV_STA_DISCONNECTED:
            /* Without retrying: STA_GAVE_UP follows, or it is by_user and wifi_disconnect()/wifi_connect() moves it. */
            if (ev->retrying) {
                now = WIFI_ST_CONNECTING;
            }
            break;
        case BM_WIFI_EV_STA_GAVE_UP:
            now = WIFI_ST_IDLE;
            break;
        default:
            break;
        }
    }
    s_state = now;
    bm_hal_lock_exit(s_lock);
    call_hook(ev, prev, now, prev != now);
    gate_exit(entered);
}

zend_class_entry *wifi_state_enum_ce(void)
{
    return s_state_ce;
}

zend_class_entry *wifi_reason_enum_ce(void)
{
    return s_reason_ce;
}

zend_object *wifi_state_case(wifi_st_t st)
{
    if ((unsigned) st >= sizeof s_states / sizeof s_states[0]) {
        st = WIFI_ST_IDLE;
    }
    return zend_enum_get_case_cstr(s_state_ce, s_states[st].name);
}

zend_object *wifi_reason_case(bm_wifi_reason_t r)
{
    if ((unsigned) r >= sizeof s_reasons / sizeof s_reasons[0]) {
        r = BM_WIFI_REASON_OTHER;
    }
    return zend_enum_get_case_cstr(s_reason_ce, s_reasons[r].name);
}
