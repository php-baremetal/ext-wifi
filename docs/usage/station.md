---
eyebrow: 'ext-wifi · PHP API'
lede:    'Scan for networks and join one as a station. wifi_connect() blocks until the board has an IP address or the timeout passes.'
see_also:
  - { href: './access-point.md', meta: 'PHP API' }
  - { href: '../reference/how-it-runs.md', meta: 'Reference' }
  - { href: '../recipes/connect-as-station.md', meta: 'Recipe' }
prev: { label: 'Overview', href: '../overview.md' }
next: { label: 'Access point', href: './access-point.md' }
---

# Station

## wifi_scan()

<!-- @method name="wifi_scan(): array|false" returns="array|false" -->

Scans every channel and returns up to 48 networks, or `false` when the scan could not start or did not finish
within 15 s. Each entry:

| Key | Type | Content |
| --- | --- | --- |
| `ssid` | `string` | Network name; `''` for a hidden network. |
| `bssid` | `string` | Access point MAC, lowercase, e.g. `a4:cf:12:3b:9e:01`. |
| `rssi` | `int` | Signal in dBm, e.g. `-58`. |
| `channel` | `int` | Primary channel. |
| `auth` | `string` | `open`, `wep`, `wpa`, `wpa2`, `wpa/wpa2`, `wpa2-ent`, `wpa3`, `wpa2/wpa3`, `wapi` or `unknown`. |

The scan turns the station role on if it was off. It blocks the script while it runs, typically a few
seconds.

## wifi_connect()

<!-- @method name="wifi_connect(string $ssid, ?string $password = null, int $timeout_ms = 30000): bool" returns="bool" -->

<!-- @params -->
<!-- @param name="$ssid" type="string" required -->
Network name, 1 to 32 bytes; any other length throws `\ValueError` (`must be 1 to 32 bytes long`).
<!-- @endparam -->
<!-- @param name="$password" type="?string" default="null" -->
The passphrase: 8 to 63 characters, or exactly 64 hex digits for a raw key. More than 64 bytes or a NUL byte throws `\ValueError`. `null` or `''` joins an open network.
<!-- @endparam -->
<!-- @param name="$timeout_ms" type="int" default="30000" -->
How long to wait for an IP address. A value of 0 or less means 30000. Before ext-wifi 1.0.2 the default was 15000.
<!-- @endparam -->
<!-- @endparams -->

Returns `true` once the board has an IP address. On the way:

- a current association is dropped first, so calling it again switches networks;
- every channel is scanned and the strongest access point with that name is chosen;
- with a password, WPA2-PSK is the minimum, and WPA3 (SAE) and WPA2/WPA3 transition networks are accepted;
- a dropped association during the attempt is retried, up to 10 times.

On timeout it stops the attempt (so the board cannot connect after the call reported failure) and returns
`false`. Wrong credentials, a missing network and a slow DHCP server all show up as `false` after
`$timeout_ms`; the driver logs the disconnect reason on the console (`sta disconnected, reason=..`).

## wifi_disconnect()

<!-- @method name="wifi_disconnect(): bool" returns="bool" -->

Leaves the network and stops retries. Always returns `true`. The station role stays on, so a scan does not
have to restart it.

## wifi_connected()

<!-- @method name="wifi_connected(): bool" returns="bool" -->

`true` while the station has an IP address. It turns `false` as soon as the link drops.

## wifi_ip() and wifi_rssi()

<!-- @method name="wifi_ip(): ?string" returns="?string" -->

The station's IPv4 address, e.g. `192.168.1.42`, or `null` when not connected.

<!-- @method name="wifi_rssi(): ?int" returns="?int" -->

The current signal of the joined access point in dBm, or `null` when not connected.

## When the link drops

When the access point goes away after a successful connect, the driver reconnects on its own, up to 10 times;
every time it gets an address the count starts again. The attempts come about every 2.5 s, so the driver
gives up when the access point is gone for more than about 25 s; after that the station stays down.
Watch `wifi_connected()` and call `wifi_connect()` again:

<!-- @code-block language="php" label="reconnect in loop()" -->
```php
<?php
function loop(int $tick): void
{
    if (!wifi_connected()) {
        echo "offline, reconnecting\n";
        wifi_connect(getenv('WIFI_SSID') ?: '', getenv('WIFI_PASSWORD') ?: null);
    }
    usleep(10000000);
}
```
<!-- @endcode-block -->
