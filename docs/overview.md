---
eyebrow: 'Package · Extension'
lede:    'Scan, join a network or open an access point from PHP. The firmware never connects by itself and keeps no credentials in its configuration. Neutral extension; conductor adds the Wi-Fi HAL for your platform.'
see_also:
  - { href: './usage/station.md', meta: 'PHP API' }
  - { href: './usage/access-point.md', meta: 'PHP API' }
  - { href: './reference/how-it-runs.md', meta: 'Reference' }
  - { href: 'package:php-baremetal/esp32-wifi-hal', meta: 'HAL', label: 'esp32-wifi-hal' }
  - { href: 'package:php-baremetal/ext-web', meta: 'Extension', label: 'ext-web' }
prev: { label: 'No previous page', href: '#' }
next: { label: 'Station', href: './usage/station.md' }
---

# Wi-Fi from PHP

ext-wifi gives the script control of the board's Wi-Fi radio. As a **station** it scans for networks and
joins one, waiting until it has an IP address. As an **access point** it opens the board's own network, with
a DHCP server, so a phone or laptop can join it directly. Both roles can run at the same time.

The SDK brings up the TCP/IP stack and the event loop on every board, but on a Wi-Fi board it never joins a
network and never reads credentials: connecting is always a call in your script. Once connected, whatever
uses the network works over it, `ext-web` included.

## Requirements

| Requires | Why |
| --- | --- |
| `php-baremetal/wifi-hal` (virtual) | The radio driver lifecycle: init, scan, station, access point. On ESP32 it is `php-baremetal/esp32-wifi-hal` (platform `esp32`, over `esp_wifi`, `esp_netif`, `esp_event`, `nvs_flash`), which also brings `php-baremetal/esp32-executor`. |

You never require the HAL yourself. Conductor reads the platform from `extra.baremetal.sdk` (`esp32@^2.0` gives
`esp32`) and adds `php-baremetal/esp32-wifi-hal` while Composer resolves. The PHP module is `wifi`. ext-wifi
does not need ext-events and works in every runtime.

## Boards

The SDK checks the board when it builds:

| Board network | ext-wifi |
| --- | --- |
| `wifi` (ESP32-S3 boards such as the Waveshare ESP32-S3-Zero, the ESP32-S3-DevKitC-1, the Waveshare ESP32-S3-Touch-LCD-1.85B) | Required to get any network. `"network": "on"` without ext-wifi is a build error: `board '...' connects over Wi-Fi from PHP: composer require php-baremetal/ext-wifi`. |
| `wifi-c6` (Waveshare ESP32-P4-WIFI6: an ESP32-C6 companion over ESP-Hosted) | Same as `wifi`; the companion link is started at boot. Builds, not yet verified on hardware. |
| Wired network on a chip with a radio (Waveshare ESP32-S3-ETH) | Optional; Wi-Fi then runs next to Ethernet. |
| No radio (Waveshare ESP32-P4-ETH, Waveshare ESP32-P4-Pico, generic ESP32-P4) | Build error: `board '...' has no Wi-Fi: remove php-baremetal/ext-wifi`. |

## Install

<!-- @steps -->
- **Install conductor once per machine**

  `composer global require php-baremetal/conductor` keeps the plugin active during the first solve of a new
  project.

- **Create the project**

  `phpbm init my-wifi` writes `composer.json` with conductor, the package registry, and the SDK, board,
  runtime and storage you pick.

- **Require the extension**

  `composer require php-baremetal/ext-wifi`

- **Build and flash**

  `phpbm flash` builds the firmware and flashes it; `phpbm monitor` opens the serial console.
<!-- @endsteps -->

The resulting `composer.json` for a Waveshare ESP32-S3-Zero (N8R8), with the credentials in a `.env` file that
is baked into the firmware and kept out of the sources and of git:

<!-- @code-block language="json" label="composer.json" -->
```json
{
  "name": "acme/my-wifi",
  "type": "project",
  "repositories": [
    { "type": "composer", "url": "https://repository.php-baremetal.com", "canonical": true }
  ],
  "require": {
    "php-baremetal/conductor": "^1.0",
    "php-baremetal/ext-wifi": "^1.0"
  },
  "config": {
    "allow-plugins": { "php-baremetal/conductor": true },
    "platform": { "php": "8.5.11" }
  },
  "extra": {
    "baremetal": {
      "sdk": "esp32@^2.0",
      "board": "waveshare-esp32-s3-zero-n8r8",
      "runtime": "init-loop",
      "storage": "embedded",
      "env": ".env"
    }
  }
}
```
<!-- @endcode-block -->

<!-- @code-block language="ini" label=".env" -->
```ini
WIFI_SSID=my-network
WIFI_PASSWORD=my-password
```
<!-- @endcode-block -->

## A first script

<!-- @code-block language="php" label="project-src/index.php" -->
```php
<?php
function setup(): void
{
    $ssid = getenv('WIFI_SSID') ?: '';
    $pass = getenv('WIFI_PASSWORD') ?: null;

    echo "connecting to '$ssid'...\n";
    if (wifi_connect($ssid, $pass)) {
        printf("connected: %s (%d dBm)\n", wifi_ip(), wifi_rssi());
    } else {
        echo "connect failed\n";
    }
}

function loop(int $tick): void
{
    echo wifi_connected() ? "online " . wifi_ip() . "\n" : "offline\n";
    usleep(5000000);
}
```
<!-- @endcode-block -->

## PHP API at a glance

| Function | Returns | Meaning |
| --- | --- | --- |
| `wifi_scan()` | `array` or `false` | Networks in range: `ssid`, `bssid`, `rssi`, `channel`, `auth`. |
| `wifi_connect(string $ssid, ?string $password = null, int $timeout_ms = 15000)` | `bool` | Join a network and wait for an IP address. |
| `wifi_disconnect()` | `bool` | Leave the network. |
| `wifi_connected()` | `bool` | Whether the station has an IP address. |
| `wifi_ip()`, `wifi_rssi()` | `?string`, `?int` | Station address and signal, `null` when not connected. |
| `wifi_ap_start(string $ssid, ?string $password = null, int $channel = 1, int $max_conn = 4)` | `bool` | Open the board's own network. |
| `wifi_ap_stop()` | `bool` | Close it. |
| `wifi_ap_ip()`, `wifi_ap_clients()` | `?string`, `int` | Access point address and number of joined stations. |
| `wifi_available()` | `bool` | Always `true` when the extension is built. |

No function throws for a Wi-Fi failure: each reports it in its return value.

## Dependencies

- Requires the virtual `php-baremetal/wifi-hal`, provided on ESP32 by
  [esp32-wifi-hal](package:php-baremetal/esp32-wifi-hal), which runs every driver call on
  [esp32-executor](package:php-baremetal/esp32-executor).
- Gives [ext-web](package:php-baremetal/ext-web) its network interface on Wi-Fi boards.

## Notes

- `wifi_scan()` and `wifi_connect()` block the script while they wait (up to 15 s for a scan, `$timeout_ms`
  for a connect). In the `event-driven` runtime nothing else is delivered meanwhile.
- With the `web-server` runtime on a Wi-Fi board, connect in the `init` script (the runtime option `init`),
  which runs once before the server starts.
- The first Wi-Fi call initialises the radio and the `nvs` flash partition. See
  [How it runs](./reference/how-it-runs.md) for what that means for stored settings.
