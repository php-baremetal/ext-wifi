---
eyebrow: 'ext-wifi · PHP API'
lede:    'Turn the board into a Wi-Fi access point with a DHCP server, alone or next to a station connection.'
see_also:
  - { href: './station.md', meta: 'PHP API' }
  - { href: '../recipes/access-point-fallback.md', meta: 'Recipe' }
prev: { label: 'Station', href: './station.md' }
next: { label: 'How it runs', href: '../reference/how-it-runs.md' }
---

# Access point

## wifi_ap_start()

<!-- @method name="wifi_ap_start(string $ssid, ?string $password = null, int $channel = 1, int $max_conn = 4): bool" returns="bool" -->

<!-- @params -->
<!-- @param name="$ssid" type="string" required -->
Network name, 1 to 32 bytes; any other length throws `\ValueError`.
<!-- @endparam -->
<!-- @param name="$password" type="?string" default="null" -->
WPA2-PSK passphrase of 8 to 63 characters, or exactly 64 hex digits (more than 64 bytes or a NUL byte throws `\ValueError`). `null`, `''` or anything
shorter than 8 characters opens the network with no password.
<!-- @endparam -->
<!-- @param name="$channel" type="int" default="1" -->
Wi-Fi channel. While the station is connected the radio follows the station's channel.
<!-- @endparam -->
<!-- @param name="$max_conn" type="int" default="4" -->
How many stations may join at once.
<!-- @endparam -->
<!-- @endparams -->

Returns `true` when the network is up, `false` when the driver refused the settings (for example an invalid
channel). Calling it again replaces the settings. With the station role also on, both run together.

<!-- @callout variant="danger" title="A short password opens the network" -->
A password under 8 characters is not an error: the access point starts **open**. Check the length yourself
when the password comes from user input.
<!-- @endcallout -->

## wifi_ap_stop()

<!-- @method name="wifi_ap_stop(): bool" returns="bool" -->

Closes the access point and returns `true`. A station connection stays up; with no role left, the radio
stops.

## wifi_ap_ip() and wifi_ap_clients()

<!-- @method name="wifi_ap_ip(): ?string" returns="?string" -->

The board's address on its own network, or `null` when the access point is not up. With the ESP-IDF defaults
it is `192.168.4.1`, and joined devices get their addresses from the board's DHCP server.

<!-- @method name="wifi_ap_clients(): int" returns="int" -->

How many stations are joined right now; `0` when the access point is down.

## wifi_available()

<!-- @method name="wifi_available(): bool" returns="bool" -->

`true` when the radio comes up. The first call brings up the Wi-Fi driver, without joining or opening a
network; `false` means it could not (on a board whose Wi-Fi is an ESP32-C6 companion, the companion did not
start). The function exists only when ext-wifi is in the firmware: to write a script that also runs without
it, test `function_exists('wifi_available')` first.

## Example

<!-- @code-block language="php" label="init-loop: an access point" -->
```php
<?php
const AP_SSID = 'php-baremetal';
const AP_PASS = 'baremetal';            // 8 characters or more

function setup(): void
{
    if (!wifi_ap_start(AP_SSID, AP_PASS)) {
        exit("cannot start the access point\n");
    }
    printf("join '%s' and reach the board at %s\n", AP_SSID, wifi_ap_ip());
}

function loop(int $tick): void
{
    printf("%d device(s) joined\n", wifi_ap_clients());
    usleep(5000000);
}
```
<!-- @endcode-block -->
