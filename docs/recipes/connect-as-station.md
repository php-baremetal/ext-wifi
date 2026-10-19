---
eyebrow: 'ext-wifi · Recipe'
lede:    'Join your network with credentials kept in a .env file that is baked into the firmware, stay online, and report the link every few seconds.'
see_also:
  - { href: '../usage/station.md', meta: 'PHP API' }
  - { href: '../reference/how-it-runs.md', meta: 'Reference' }
prev: { label: 'How it runs', href: '../reference/how-it-runs.md' }
next: { label: 'Scan and pick a network', href: './scan-and-pick-a-network.md' }
---

# Connect with credentials from .env

A Waveshare ESP32-S3-Zero (N8R8) in the `init-loop` runtime. The SSID and password live in `.env` next to
`composer.json`: the build bakes the file into the firmware as environment variables, so they stay out of
`project-src/` and, with a `.gitignore` entry, out of git.

## The project

<!-- @code-block language="json" label="composer.json" -->
```json
{
  "name": "acme/wifi-connect",
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
      "sdk": "esp32@^1.0",
      "board": "waveshare-esp32-s3-zero-n8r8",
      "runtime": "init-loop",
      "storage": "embedded",
      "env": ".env",
      "options": { "loop-interval": 5000 }
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

Leave `WIFI_PASSWORD` empty for an open network. Changing `.env` needs a new `phpbm flash`.

## The script

<!-- @code-block language="php" label="project-src/index.php" -->
```php
<?php
function join_network(): bool
{
    $ssid = getenv('WIFI_SSID') ?: '';
    $pass = getenv('WIFI_PASSWORD') ?: null;
    if ($ssid === '') {
        echo "WIFI_SSID is not set in .env\n";
        return false;
    }
    echo "connecting to '$ssid'...\n";
    return wifi_connect($ssid, $pass, 20000);
}

function setup(): void
{
    if (join_network()) {
        printf("connected: %s (%d dBm)\n", wifi_ip(), wifi_rssi());
    } else {
        echo "connect failed: check the credentials and the signal\n";
    }
}

function loop(int $tick): void
{
    if (wifi_connected()) {
        printf("tick %d: %s, %d dBm\n", $tick, wifi_ip(), wifi_rssi());
        return;
    }
    echo "tick $tick: offline, retrying\n";
    join_network();
}
```
<!-- @endcode-block -->

`loop-interval` 5000 makes the runtime wait 5 s between two `loop()` calls.

## When it fails

- **`false` after 20 s**: wrong password, the network is out of range, or no DHCP answer. The console shows
  `sta disconnected, reason=..` lines from the driver; the reason code says which.
- **A 2.4 GHz network is required**: the ESP32-S3 radio does not see 5 GHz networks, so they are missing from
  `wifi_scan()` too.
- **A 32-byte SSID** cannot be joined: only the first 31 bytes are used.
