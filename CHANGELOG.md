# Changelog

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
