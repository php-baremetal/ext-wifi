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
Network name; the first 31 bytes are used.
<!-- @endparam -->
<!-- @param name="$password" type="?string" default="null" -->
WPA2-PSK passphrase of 8 characters or more. `null`, `''` or anything shorter than 8 characters opens the
network with no password.
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

Always `true`: the function exists only when ext-wifi is in the firmware. To write a script that also runs
without ext-wifi, test `function_exists('wifi_available')` instead.

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
