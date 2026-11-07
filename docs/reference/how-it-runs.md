---
eyebrow: 'ext-wifi · Reference'
lede:    'Every Wi-Fi driver call runs on the executor task, waits run on the PHP task, and the first call sets up the radio and the nvs partition. What that means for blocking, flash, power-save and limits.'
see_also:
  - { href: '../usage/station.md', meta: 'PHP API' }
  - { href: 'package:php-baremetal/esp32-wifi-hal', meta: 'HAL', label: 'esp32-wifi-hal' }
  - { href: 'package:php-baremetal/esp32-executor', meta: 'Library', label: 'esp32-executor' }
prev: { label: 'Events', href: '../usage/events.md' }
next: { label: 'Connect with credentials from .env', href: '../recipes/connect-as-station.md' }
---

# How it runs

## Where the work happens

The PHP task keeps its stack in PSRAM, and on ESP32 a task with a PSRAM stack must not run code that touches
the flash, because the flash cache is suspended meanwhile. The Wi-Fi driver may touch the flash at any call
(settings in NVS, PHY calibration data). So on ESP32 every `esp_wifi_*`, `esp_netif_*` and `nvs_flash_*` call
of the HAL is handed to the **executor** task of `esp32-executor` (core 0, stack in internal RAM), and the PHP
task waits for the result.

| What | Runs on |
| --- | --- |
| Driver calls: init, mode changes, scan start and fetch, connect, disconnect, access point, signal and client queries | executor task |
| Waiting for the scan to finish, and for the IP address after a connect | PHP task (blocked) |
| Driver events: scan done, connected, disconnected (and the automatic retry), got and lost IP, access point started, stopped, station joined and left | ESP-IDF default event loop task; never PHP |
| ext-wifi's state and its events (`wifi_events()`): updated, and the event posted to the bus, on the task the driver event comes from | ESP-IDF default event loop task, or the PHP task for a change made by a PHP function |
| The event objects, filled (SSIDs, access point IP, scan list read from the driver) and delivered to the listeners | PHP task, in the `event-driven` reactor |

A state change and the posting of its `StateChanged` happen together under one task-level lock of ext-wifi,
taken by the driver's event task and by the PHP functions that move the state (`wifi_connect()`,
`wifi_disconnect()`), so the `StateChanged` events always reach the queue in the order of the changes. The
lock is a queue with no priority inheritance; the driver's event task waits for it only while another task
is changing the state, briefly.

If the executor's queue stays full for one second, the call fails and the function reports it as `false`
(or `null`, `0`).

## Blocking

`wifi_scan()` waits up to 15 s, `wifi_connect()` up to `$timeout_ms`; with `wait: false` both return at once
and the outcome comes as an [event](../usage/events.md). The script is blocked meanwhile:

- `init-loop`: the next `loop()` runs after the call returns;
- `event-driven`: no event is delivered until the call returns (the Wi-Fi events raised meanwhile wait in the
  queue); HTTP requests from `ext-web` queue up and may hit their own deadline;
- `web-server`: connect in the `init` script, before the server starts.

The other functions return within milliseconds.

## First use

The first Wi-Fi call (any of `wifi_available()`, scan, connect, access point) initialises, on the executor:

<!-- @steps -->
- **The nvs partition**

  `nvs_flash_init()` on the firmware's `nvs` partition. If the partition is full or was written by a newer
  format, it is erased and initialised again.

- **The Wi-Fi driver**

  `esp_wifi_init()` with the ESP-IDF defaults, and the event handlers for scan, disconnect and got-IP. The
  driver keeps its configuration in RAM only (`WIFI_STORAGE_RAM`): SSIDs and passwords are never written to
  the `nvs` partition.

- **The interface, on demand**

  The station interface is created by the first scan or connect, the access point interface by the first
  `wifi_ap_start()`. Both stay for the whole run.
<!-- @endsteps -->

If that fails, every function reports failure (`false`, `null`, `0`). The TCP/IP stack and the default event
loop are already running: the SDK starts them at boot on every board.

## Credentials

The SDK has no setting for Wi-Fi credentials, and the HAL keeps none: the SSID and password are passed in each
`wifi_connect()` / `wifi_ap_start()` call. Where they come from is up to the script:

| Source | How |
| --- | --- |
| A `.env` file baked into the firmware | `"env": ".env"` in `extra.baremetal`; read with `getenv()`. Kept out of the sources and of `phpbm sync`. |
| The persistent store | `ext-store`: `store_get('wifi.ssid')`. See [Access point fallback](../recipes/access-point-fallback.md). |
| A setup page | `ext-web` on an access point collects them, then `ext-store` keeps them. |

The ESP-IDF Wi-Fi driver keeps its station and access point settings in RAM only (esp32-wifi-hal 1.0.2 and
later): SSIDs and passwords are never written to flash. Earlier versions let the driver record them, in
clear, in its own namespace of the `nvs` partition; those entries stay until the partition is erased.
ext-wifi never connects at boot.

## Power-save

ext-wifi does not change the radio's power mode: the ESP-IDF default (modem sleep while the station is
connected) applies, with or without the SDK option `power-save`. Wi-Fi with `power-save` on is not yet
verified on hardware.

## Limits

| Limit | Value | Source |
| --- | --- | --- |
| Networks returned by one scan | 48 | ext-wifi |
| Scan timeout | 15 s | esp32-wifi-hal |
| Default connect timeout | 30000 ms | ext-wifi |
| Retries of a dropped association | `$retries` of `wifi_connect()` (10 by default, `-1` forever), about 2.5 s apart, refilled at every address the station gets; none after `wifi_disconnect()` or a connect timeout | ext-wifi, esp32-wifi-hal |
| Event classes | 9 (`wifi_events()`), out of the 32 event classes the sources of the bus can use | ext-wifi, ext-events |
| Data carried by an event | at most 16 bytes in the event slot (the states, reason and code, an IP, a MAC and a count); the SSIDs, the access point IP and the scan list are read when the event is delivered | ext-events |
| `$code` of `ConnectFailed` and `Disconnected` | the driver's code, clamped to -32767..32767 | ext-wifi |
| `$clients` of the access point events | clamped to 255 | ext-wifi |
| SSID | 1 to 32 bytes, with no NUL byte, else `\ValueError` | ext-wifi |
| Password | at most 64 bytes, with no NUL byte, else `\ValueError`; 8 to 63 characters, or exactly 64 hex digits | ext-wifi |
| Access point password | 8 characters minimum, else the network is open | esp32-wifi-hal |
| Wait for the executor queue | 1000 ms | esp32-wifi-hal |
