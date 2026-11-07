---
eyebrow: 'ext-wifi · PHP API'
lede:    'React to the radio instead of polling it: wifi_events() puts the station state, connection failures, drops, scan results and access point clients on the event bus.'
see_also:
  - { href: './station.md', meta: 'PHP API' }
  - { href: './access-point.md', meta: 'PHP API' }
  - { href: '../reference/how-it-runs.md', meta: 'Reference' }
  - { href: 'package:php-baremetal/ext-events', meta: 'Extension', label: 'ext-events' }
prev: { label: 'Access point', href: './access-point.md' }
next: { label: 'How it runs', href: '../reference/how-it-runs.md' }
---

# Events

The functions of ext-wifi work in every runtime and answer when they are called. In the `event-driven`
runtime the board can also tell the script what happens on the radio: the station connects or drops, a
connect gives up, a scan ends, a phone joins the access point. Each of these is an event class of
`Baremetal\Wifi`, delivered to the listeners registered with `Events::listen()` of
[ext-events](package:php-baremetal/ext-events).

## wifi_events()

<!-- @method name="wifi_events(): Baremetal\Wifi\Events" returns="Baremetal\Wifi\Events" -->

Registers the nine event classes as sources of the event bus and turns their emission on. Nothing is emitted
before the first call. It returns a `Baremetal\Wifi\Events`, a `Baremetal\Source` handle:

| Method | Effect |
| --- | --- |
| `stop()` | No more Wi-Fi events are emitted. `wifi_state()` and the other functions keep working. |
| `start()` | Emission on again. |
| `running()` | Whether events are emitted. |

Every call returns a new handle on the same source and turns the emission on; dropping a handle leaves it
running. The nine classes take nine of the 32 event classes the sources of the bus can use.

`wifi_events()` throws in the `web-server` runtime, where no reactor delivers events, like the other sources
(`wifi_events() is not available in the web-server runtime: event sources need the event-driven runtime`),
and an `\Exception` when the source table of the bus is full (`wifi_events(): unknown class '...'
or too many sources`).

## The station state

`wifi_state()` returns a `Baremetal\Wifi\State`:

| Case | Value | Meaning |
| --- | --- | --- |
| `State::Idle` | `idle` | No network asked for. |
| `State::Connecting` | `connecting` | `wifi_connect()` is associating, also during the automatic retries. |
| `State::Connected` | `connected` | Associated, no IP address yet. |
| `State::Online` | `online` | Associated, with an IP address. |

How it moves:

| What happens | New state |
| --- | --- |
| `wifi_connect()` | `Connecting` |
| The station associates | `Connected` |
| The station gets an IP address | `Online` |
| The IP address goes while still associated | `Connected` (from `Online`) |
| The connection drops and the driver retries (`Disconnected::$retrying` true) | `Connecting` |
| A connect gives up (`ConnectFailed`) | `Idle` |
| `wifi_disconnect()`, or `wifi_connect()` with `wait: true` that returns `false` | `Idle` |

Only `wifi_connect()` leaves `Idle`. The access point has no state of its own: `wifi_ap_active()` says
whether it is up.

## The reasons

`ConnectFailed` and `Disconnected` carry a `Baremetal\Wifi\Reason`, translated from the driver's reason code
(the raw code stays in `$code`):

| Case | Value | When |
| --- | --- | --- |
| `Reason::WrongPassword` | `wrong-password` | The access point refused the password, before the connect ever had an IP address. A weak link or a busy access point can cause one refusal with a correct password, so the connect gives up with this reason only after two refusals in a row (or when no retry is left). |
| `Reason::NotFound` | `not-found` | No access point with that SSID in range, before the station associated. |
| `Reason::Lost` | `lost` | A connection that was up went away (out of range, access point switched off): once the station was associated, every end the script did not ask for that is not a password refusal. After an IP address, an authentication failure on a reconnect also counts as lost. |
| `Reason::ByUser` | `by-user` | The script closed it: `wifi_disconnect()`, or a new `wifi_connect()`. |
| `Reason::Other` | `other` | Anything else, and a `wait: true` connect that timed out. |

## The event classes

All are `final`, extend the abstract `Baremetal\Wifi\Event` (a `Baremetal\Event`, so they also have `$time`)
and have `public readonly` properties:

| Class | When | Properties |
| --- | --- | --- |
| `StateChanged` | `wifi_state()` changed (never for an equal state) | `State $state`, `State $previous`, `?string $ssid` (`null` in `Idle`) |
| `Online` | The station has an IP address | `string $ip`, `string $ssid` |
| `ConnectFailed` | A connect gave up: the retries ran out (none left after a failed or dropped association), the password was refused twice in a row (at once, whatever `$retries` is, `-1` included), or a `wait: true` connect timed out | `Reason $reason`, `int $code`, `string $ssid` |
| `Disconnected` | A connection that was associated ended, dropped or closed by the script | `Reason $reason`, `int $code`, `bool $retrying` (the driver reconnects by itself) |
| `ScanDone` | A scan ended: a `wifi_scan(wait: false)`, and also a waiting `wifi_scan()` (delivered after the call returns) | `array $networks`, as `wifi_scan()` returns it; `[]` when the scan failed |
| `ApStarted` | The access point is up | `string $ssid`, `string $ip` |
| `ApStopped` | The access point went down | `int $clients` (how many were joined) |
| `ClientJoined` | A station joined the access point | `string $mac` (`aa:bb:cc:dd:ee:ff`), `int $clients` (how many now) |
| `ClientLeft` | A station left the access point | `string $mac`, `int $clients` |

For one change several events come in this order: `StateChanged` first, then the event of what happened
(`Online` after the `StateChanged` to `Online`). The exception is `wifi_disconnect()`: the `Disconnected`
(`Reason::ByUser`) usually comes before the `StateChanged` to `Idle`, and `wifi_state()` is still `Online`
when it is delivered. The `StateChanged` events form one chain: each `$previous` is the `$state` of the one
before, whether the change came from the driver or from a PHP function, so the last one delivered matches
`wifi_state()` once the queue is empty, unless events were dropped for lack of a slot (`Events::stats()`
counts them, and the first drop of a class prints a warning).

A few rules worth knowing:

- **Read at delivery.** The SSIDs (`StateChanged::$ssid`, `Online::$ssid`, `ConnectFailed::$ssid`),
  `ApStarted::$ip` and `ScanDone::$networks` are read from the driver when the event is delivered, not when it
  happened. If another scan starts meanwhile, `ScanDone` carries the newest results; if the script connects to
  another network first, the SSID is the new one; if it called `wifi_disconnect()` first, `Online::$ssid` and
  `ConnectFailed::$ssid` are `''`.
- **A start that is refused is not an event.** A `wifi_connect(wait: false)` or `wifi_scan(wait: false)` that
  the driver refuses to start returns `false` and raises no `ConnectFailed` or `ScanDone`: check the return
  value.
- **No scan while the station retries.** While the state is `Connecting` (associating, or between retries)
  the driver refuses a scan, so `wifi_scan()` returns `false`; with `retries: -1` and no access point in range
  that lasts for good. Call `wifi_disconnect()`, scan, then connect again.
- **A cancelled connect is not a failure.** A `wifi_connect()` while another is in progress cancels it:
  `Disconnected` with `Reason::ByUser` if it was associated, nothing if it was still associating, and never
  `ConnectFailed`.
- **`ClientLeft` may or may not arrive for the stations dropped by `wifi_ap_stop()`**, before or instead of
  `ApStopped` (whose `$clients` counts the stations joined before the stop). Reset a list of clients kept by
  the script on `ApStopped`.
- **The station does not move the access point.** Switching the station on (a scan or a connect while the
  access point is up) makes the driver restart the access point internally; that raises no `ApStopped` or
  `ApStarted`, and the joined stations stay counted. Each `wifi_ap_start()` raises one `ApStarted`, also when
  it replaces the settings of an access point already up.
- **`wifi_ap_stop()` with the access point down** returns `true` and emits nothing.
- **`$code`** is the driver's raw reason code (on ESP32 the `wifi_err_reason_t` value), clamped to ±32767;
  `0` when there is none.
- **Blocking calls still block.** `wifi_connect()` with `wait: true` and `wifi_scan()` hold the script; the
  events raised meanwhile are delivered after the call returns.

## Example

<!-- @code-block language="php" label="event-driven: connect without blocking" -->
```php
<?php
use Baremetal\Events;
use Baremetal\Wifi\{StateChanged, Online, ConnectFailed, Disconnected, ScanDone};

wifi_events();

Events::listen(StateChanged::class, fn (StateChanged $e) =>
    printf("%s -> %s\n", $e->previous->value, $e->state->value));
Events::listen(Online::class, fn (Online $e) => printf("online on %s: %s\n", $e->ssid, $e->ip));
Events::listen(ConnectFailed::class, fn (ConnectFailed $e) =>
    printf("cannot join %s: %s (%d)\n", $e->ssid, $e->reason->value, $e->code));
Events::listen(Disconnected::class, fn (Disconnected $e) =>
    printf("dropped: %s%s\n", $e->reason->value, $e->retrying ? ', retrying' : ''));

// The driver refuses a scan and a connect at the same time: scan first, connect once the scan is done.
$connect = function (): void {
    static $started = false;
    if ($started) {
        return;
    }
    $started = true;
    if (!wifi_connect(getenv('WIFI_SSID') ?: '', getenv('WIFI_PASSWORD') ?: null, wait: false, retries: -1)) {
        echo "the connect did not start\n";   // a refused start raises no ConnectFailed
    }
};
Events::listen(ScanDone::class, function (ScanDone $e) use ($connect): void {
    printf("%d networks in range\n", count($e->networks));
    $connect();
});

if (!wifi_scan(wait: false)) {
    echo "the scan did not start\n";
    $connect();
}
```
<!-- @endcode-block -->

With `retries: -1` the station keeps trying after every drop: switch the access point off and on again and
the script sees `Disconnected` (`lost`, retrying), the `StateChanged` to `connecting` and, once it is back,
`Online`.
