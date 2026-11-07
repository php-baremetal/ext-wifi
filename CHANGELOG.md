# Changelog

## 1.1.0

- Added: Wi-Fi events on the event bus of ext-events, after `wifi_events()`, which returns a
  `Baremetal\Wifi\Events` (a `Baremetal\Source`: `stop()`, `start()`, `running()`). Nine `final` classes
  extending the abstract `Baremetal\Wifi\Event`: `StateChanged` (`$state`, `$previous`, `$ssid`), `Online`
  (`$ip`, `$ssid`), `ConnectFailed` (`$reason`, `$code`, `$ssid`), `Disconnected` (`$reason`, `$code`,
  `$retrying`), `ScanDone` (`$networks`), `ApStarted` (`$ssid`, `$ip`), `ApStopped` (`$clients`), `ClientJoined`
  and `ClientLeft` (`$mac`, `$clients`). Nothing is emitted before the first call; `wifi_events()` throws in the
  `web-server` runtime like the other sources.
- Added: the enums `Baremetal\Wifi\State` (`Idle`, `Connecting`, `Connected`, `Online`) and
  `Baremetal\Wifi\Reason` (`WrongPassword`, `NotFound`, `Lost`, `ByUser`, `Other`).
- Added: `wifi_connect()` takes `bool $wait = true` (`false` starts the connect and returns at once; the outcome
  comes as `Online` or `ConnectFailed`) and `int $retries = 10` (automatic retries after a failed or dropped
  association, refilled at every IP; `0` none, `-1` forever; below `-1` a `\ValueError`). Two password
  refusals in a row before the first IP give up at once with `Reason::WrongPassword`, whatever `$retries` is
  (`-1` included); a single refusal is retried. A connect while another is in progress cancels it. With the
  defaults nothing else changes: a password refused twice in a row now fails at once, where 1.0.2 retried it
  up to 10 times.
- Added: `wifi_scan(bool $wait = true)`: `false` starts a scan and returns whether it started; the list comes in
  `ScanDone`.
- Added: `wifi_state(): Baremetal\Wifi\State`, `wifi_ssid(): ?string` (the network asked for or in use, `null`
  in `Idle`) and `wifi_ap_active(): bool`.
- Changed: requires `php-baremetal/ext-events` ^1.1 (the event bus; `bm_events_set_zval()` for the fillers),
  `php-baremetal/events-hal` ^1.3 (the lock of the station state) and `php-baremetal/wifi-hal` ^1.1 (driver
  events, `bm_wifi_hal_sta_connect_ex()`, the asynchronous scan, `bm_wifi_hal_sta_ssid()`,
  `bm_wifi_hal_ap_active()`; esp32-wifi-hal 1.1.0). The functions still work in every runtime.
- Changed: the module version is `1.1.0` (was `1.0`).

## 1.0.2

- Changed: the default `$timeout_ms` of `wifi_connect()` is 30000 (was 15000), and a value of 0 or less
  also means 30000. Joining a WPA3 network with a weak radio could take longer than 15 s.
- Fixed: `wifi_available()` always returned `true`. It now says whether the radio comes up (with an ESP32-C6
  companion that did not start, `false`).
- Fixed: SSIDs and passwords were not checked. `wifi_connect()` and `wifi_ap_start()` throw `\ValueError` for
  an SSID that is not 1 to 32 bytes, a password over 64 bytes, and a NUL byte in either (a password starting
  with NUL opened the access point); a 32-byte SSID now works (with esp32-wifi-hal 1.0.2).
- Fixed: Reflection reported the optional parameters of `wifi_connect()` and `wifi_ap_start()` without their
  defaults (`null`, `30000`, `1`, `4`).

## 1.0.1

- Changed: the author's contact email.
- Docs: projects use `"sdk": "esp32@^2.0"`.

## 1.0.0

Initial release.
