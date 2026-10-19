---
eyebrow: 'ext-wifi · Recipe'
lede:    'List the networks in range, then join the strongest of the ones you know.'
see_also:
  - { href: '../usage/station.md', meta: 'PHP API' }
  - { href: './connect-as-station.md', meta: 'Recipe' }
prev: { label: 'Connect with credentials from .env', href: './connect-as-station.md' }
next: { label: 'Access point fallback', href: './access-point-fallback.md' }
---

# Scan and pick a network

Use the project of [Connect with credentials from .env](./connect-as-station.md), with two known networks in
`.env`:

<!-- @code-block language="ini" label=".env" -->
```ini
WIFI_SSID=home
WIFI_PASSWORD=home-password
WIFI_SSID_2=workshop
WIFI_PASSWORD_2=workshop-password
```
<!-- @endcode-block -->

## The script

<!-- @code-block language="php" label="project-src/index.php" -->
```php
<?php
function known_networks(): array
{
    $known = [];
    foreach (['', '_2'] as $suffix) {
        $ssid = getenv("WIFI_SSID$suffix") ?: '';
        if ($ssid !== '') {
            $known[$ssid] = getenv("WIFI_PASSWORD$suffix") ?: null;
        }
    }
    return $known;
}

function setup(): void
{
    $aps = wifi_scan();
    if ($aps === false) {
        exit("scan failed\n");
    }

    usort($aps, fn (array $a, array $b): int => $b['rssi'] <=> $a['rssi']);   // strongest first
    foreach ($aps as $ap) {
        printf("%-32s %-17s ch%-3d %4d dBm  %s\n",
            $ap['ssid'] !== '' ? $ap['ssid'] : '(hidden)', $ap['bssid'], $ap['channel'], $ap['rssi'], $ap['auth']);
    }

    $known = known_networks();
    foreach ($aps as $ap) {
        if (array_key_exists($ap['ssid'], $known)) {
            echo "joining {$ap['ssid']}...\n";
            if (wifi_connect($ap['ssid'], $known[$ap['ssid']])) {
                printf("connected: %s\n", wifi_ip());
                return;
            }
        }
    }
    echo "no known network answered\n";
}

function loop(int $tick): void
{
    echo wifi_connected() ? "online\n" : "offline\n";
}
```
<!-- @endcode-block -->

## Notes

- A network served by several access points is listed once per access point (BSSID). `wifi_connect()` picks the strongest BSSID with that name by itself.
- `auth` tells whether a password is needed: `open` needs none, `wpa2-ent` (enterprise) cannot be joined with
  `wifi_connect()`.
- The scan returns at most 48 networks.
