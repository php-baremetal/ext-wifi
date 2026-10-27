---
eyebrow: 'ext-wifi · Recipe'
lede:    'At boot, join the network saved in the persistent store; if there is none or it fails, open a setup access point so a phone can join the board.'
see_also:
  - { href: '../usage/access-point.md', meta: 'PHP API' }
  - { href: 'package:php-baremetal/ext-store', meta: 'Extension', label: 'ext-store' }
  - { href: 'package:php-baremetal/ext-web', meta: 'Extension', label: 'ext-web' }
prev: { label: 'Scan and pick a network', href: './scan-and-pick-a-network.md' }
next: { label: 'No next page', href: '#' }
---

# Access point fallback

A device shipped to someone else does not know their Wi-Fi. Here the credentials live in `ext-store`
(key/value storage that survives restarts); when they are missing or do not work, the board opens the
network `php-device-setup` instead.

## The project

<!-- @code-block language="json" label="composer.json" -->
```json
{
  "name": "acme/wifi-fallback",
  "type": "project",
  "repositories": [
    { "type": "composer", "url": "https://repository.php-baremetal.com", "canonical": true }
  ],
  "require": {
    "php-baremetal/conductor": "^1.0",
    "php-baremetal/ext-store": "^1.0",
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
      "options": { "loop-interval": 5000 }
    }
  }
}
```
<!-- @endcode-block -->

## The script

<!-- @code-block language="php" label="project-src/index.php" -->
```php
<?php
const AP_SSID = 'php-device-setup';
const AP_PASS = 'setup-1234';           // 8 characters or more, or the network is open

function setup(): void
{
    $ssid = store_get('wifi.ssid');
    $pass = store_get('wifi.pass');

    if ($ssid !== null && wifi_connect($ssid, $pass, 20000)) {
        echo 'online: ', wifi_ip(), "\n";
        return;
    }

    if (strlen(AP_PASS) < 8 || !wifi_ap_start(AP_SSID, AP_PASS)) {
        exit("cannot start the setup network\n");
    }
    echo 'join ', AP_SSID, ', then open http://', wifi_ap_ip(), "/\n";
}

function loop(int $tick): void
{
    if (wifi_ap_ip() !== null) {
        echo wifi_ap_clients(), " device(s) on the setup network\n";
    }
}
```
<!-- @endcode-block -->

On the first boot nothing is stored, so the setup network comes up at `192.168.4.1`.

## Saving the credentials

Whatever collects them (a form served with `ext-web` on the setup network, a serial command), store them and
restart:

<!-- @code-block language="php" label="after the user submitted the form" -->
```php
<?php
store_set('wifi.ssid', $ssid);
store_set('wifi.pass', $pass);
```
<!-- @endcode-block -->

A failed `wifi_connect()` already stopped its own attempt, so the access point does not compete with a late
connection to the saved network.

## Both at once

To keep the setup network while connected, for example to let the user change networks later, call
`wifi_ap_start()` after `wifi_connect()` succeeds as well. Both roles then run together and the access point
uses the station's channel; `wifi_ap_stop()` closes it and leaves the station connected.
