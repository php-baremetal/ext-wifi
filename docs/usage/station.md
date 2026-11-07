---
eyebrow: 'ext-wifi · PHP API'
lede:    'Scan for networks and join one as a station, waiting for the result or not; read the station state and the network in use.'
see_also:
  - { href: './access-point.md', meta: 'PHP API' }
  - { href: './events.md', meta: 'PHP API' }
  - { href: '../reference/how-it-runs.md', meta: 'Reference' }
  - { href: '../recipes/connect-as-station.md', meta: 'Recipe' }
prev: { label: 'Overview', href: '../overview.md' }
next: { label: 'Access point', href: './access-point.md' }
---

# Station

## wifi_scan()

<!-- @method name="wifi_scan(bool $wait = true): array|bool" returns="array|bool" -->

<!-- @params -->
<!-- @param name="$wait" type="bool" default="true" -->
`true` waits for the scan and returns its list; `false` starts it and returns at once. Since ext-wifi 1.1.0.
<!-- @endparam -->
<!-- @endparams -->

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

With `wait: false` it returns `true` once the scan has started, `false` when it cannot start (one is already
running, the station is connecting, or the radio does not come up); a scan that did not start raises no
`ScanDone`. The list comes in the `ScanDone` event (`$networks`, same entries) after
[wifi_events()](./events.md); without the events, a later `wifi_scan()` scans again. A waiting scan also
raises `ScanDone`, delivered after the call returns.

No scan is possible while the station is connecting, the automatic retries included: the driver refuses it
and `wifi_scan()` returns `false`. With `retries: -1` and no access point in range that lasts for good; call
`wifi_disconnect()`, scan, then connect again.

## wifi_connect()

<!-- @method name="wifi_connect(string $ssid, ?string $password = null, int $timeout_ms = 30000, bool $wait = true, int $retries = 10): bool" returns="bool" -->

<!-- @params -->
<!-- @param name="$ssid" type="string" required -->
Network name, 1 to 32 bytes; any other length throws `\ValueError` (`must be 1 to 32 bytes long`).
<!-- @endparam -->
<!-- @param name="$password" type="?string" default="null" -->
The passphrase: 8 to 63 characters, or exactly 64 hex digits for a raw key. More than 64 bytes or a NUL byte throws `\ValueError`. `null` or `''` joins an open network.
<!-- @endparam -->
<!-- @param name="$timeout_ms" type="int" default="30000" -->
How long to wait for an IP address, with `$wait` true only. A value of 0 or less means 30000. Before ext-wifi 1.0.2 the default was 15000.
<!-- @endparam -->
<!-- @param name="$wait" type="bool" default="true" -->
`true` waits for the IP address. `false` starts the connect and returns at once: `true` when it started,
`false` when it could not. The outcome comes as the `Online` or `ConnectFailed` event; a start refused by the
driver returns `false` and raises no `ConnectFailed`. Since ext-wifi 1.1.0.
<!-- @endparam -->
<!-- @param name="$retries" type="int" default="10" -->
Automatic retries after a failed or dropped association, the first association included (the first attempt
itself is not counted). The count starts again at every IP address. `0` never retries, `-1` retries forever.
Two password refusals in a row, before the connect ever had an IP address, give up at once, whatever
`$retries` is, `-1` included: `ConnectFailed` with `Reason::WrongPassword`. A single refusal (a weak link or a
busy access point can cause one with a correct password) is retried like any other reason; with no retry left
the connect gives up with `Reason::WrongPassword`. Every other reason, a missing network included, uses the
retries. A value below `-1` throws `\ValueError` (`wifi_connect(): $retries must be -1 (forever) or more`). Since ext-wifi 1.1.0.
<!-- @endparam -->
<!-- @endparams -->

With the defaults it behaves as before ext-wifi 1.1.0, with one difference: a password refused twice in a row
before the first IP address now ends the attempt at once (before 1.1.0 it was retried up to 10 times). A
wrong password fails sooner; two refusals in a row with a correct password, on a weak link, fail too. It
returns `true` once the board has an IP address. On the way:

- a current association is dropped first, so calling it again switches networks;
- every channel is scanned and the strongest access point with that name is chosen;
- with a password, WPA2-PSK is the minimum, and WPA3 (SAE) and WPA2/WPA3 transition networks are accepted;
- a failed or dropped association during the attempt is retried, up to `$retries` times (two password
  refusals in a row are not retried further).

A call while another connect is in progress cancels it (with the events, `Disconnected` with `Reason::ByUser`
if it was associated, never `ConnectFailed`).

On timeout it stops the attempt (so the board cannot connect after the call reported failure) and returns
`false`. Wrong credentials, a missing network and a slow DHCP server all show up as `false`, after
`$timeout_ms` at the latest; the driver logs the disconnect reason on the console (`sta disconnected, reason=..`), and
`ConnectFailed::$reason` tells them apart (`wrong-password`, `not-found`; `other` for the timeout).

## wifi_disconnect()

<!-- @method name="wifi_disconnect(): bool" returns="bool" -->

Leaves the network, or cancels a connect in progress, and stops retries. Always returns `true`. The state goes
to `Idle`; with the events, `Disconnected` (`Reason::ByUser`, `$retrying` false) comes when the station was
associated. The station role stays on, so a scan does not have to restart it.

## wifi_connected()

<!-- @method name="wifi_connected(): bool" returns="bool" -->

`true` while the station has an IP address. It turns `false` as soon as the link drops.

## wifi_state() and wifi_ssid()

<!-- @method name="wifi_state(): Baremetal\Wifi\State" returns="Baremetal\Wifi\State" -->

The station state: `State::Idle` (no network asked for), `State::Connecting` (associating, also during the
retries), `State::Connected` (associated, no IP address yet) or `State::Online` (with an IP address). It is
kept up to date in every runtime, with or without [wifi_events()](./events.md), which also lists how it moves.
Since ext-wifi 1.1.0.

<!-- @method name="wifi_ssid(): ?string" returns="?string" -->

The network asked for or in use, or `null` in `Idle`. Since ext-wifi 1.1.0.

## wifi_ip() and wifi_rssi()

<!-- @method name="wifi_ip(): ?string" returns="?string" -->

The station's IPv4 address, e.g. `192.168.1.42`, or `null` when not connected.

<!-- @method name="wifi_rssi(): ?int" returns="?int" -->

The current signal of the joined access point in dBm, or `null` when not connected.

## When the link drops

When the access point goes away after a successful connect, the driver reconnects on its own, up to
`$retries` times (10 by default); every time it gets an address the count starts again. The attempts come
about every 2.5 s, so with the default the driver gives up when the access point is gone for more than about
25 s; after that the station stays down (`State::Idle`). With `retries: -1` it never gives up. Otherwise watch
`wifi_connected()` and call `wifi_connect()` again:

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

In the `event-driven` runtime, [the events](./events.md) say the same without polling: `Disconnected` with
`$retrying` for each drop, `ConnectFailed` when the retries run out.
