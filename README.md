# ext-wifi

Wi-Fi from PHP: scan for networks, join one as a station, or open the board's own access point, with plain
functions (`wifi_scan()`, `wifi_connect()`, `wifi_ap_start()`, ...), and with events on the event bus of
ext-events in the `event-driven` runtime (`wifi_events()`). The firmware never connects to Wi-Fi by
itself and stores no credentials in its configuration: your script decides when and where to connect, with
credentials from wherever you keep them (a `.env` baked into the firmware, `ext-store`, a setup page).

ext-wifi is platform-neutral: it requires the virtual `php-baremetal/wifi-hal`, and conductor adds the HAL for
your SDK platform (`php-baremetal/esp32-wifi-hal` on ESP32). Boards with a Wi-Fi radio only.

## Install

In a project created with `phpbm init`:

```bash
composer require php-baremetal/ext-wifi
```

## Example

```php
<?php
$ssid = getenv('WIFI_SSID') ?: '';
$pass = getenv('WIFI_PASSWORD') ?: null;   // null or '' for an open network

if (wifi_connect($ssid, $pass, 20000)) {
    printf("online: %s (%d dBm)\n", wifi_ip(), wifi_rssi());
} else {
    wifi_ap_start('my-board-setup', 'setup-1234');
    printf("no network; join my-board-setup and open http://%s/\n", wifi_ap_ip());
}
```

Without blocking, in the `event-driven` runtime:

```php
<?php
use Baremetal\Events;
use Baremetal\Wifi\{Online, ConnectFailed};

wifi_events();
Events::listen(Online::class, fn (Online $e) => printf("online: %s\n", $e->ip));
Events::listen(ConnectFailed::class, fn (ConnectFailed $e) => printf("failed: %s\n", $e->reason->value));
wifi_connect(getenv('WIFI_SSID') ?: '', getenv('WIFI_PASSWORD') ?: null, wait: false, retries: -1);
```

## Documentation

- [Overview](docs/overview.md): requirements, boards, install, a first project.
- [Station](docs/usage/station.md): `wifi_scan()`, `wifi_connect()`, `wifi_ip()`, `wifi_rssi()`, ...
- [Access point](docs/usage/access-point.md): `wifi_ap_start()`, `wifi_ap_ip()`, `wifi_ap_clients()`, ...
- [Events](docs/usage/events.md): `wifi_events()`, the station state, `Online`, `ConnectFailed`, `Disconnected`,
  `ScanDone` and the access point events, for the `event-driven` runtime.
- [How it runs](docs/reference/how-it-runs.md): blocking calls, the executor, flash, power-save, limits.
- Recipes: [connect with credentials from .env](docs/recipes/connect-as-station.md),
  [scan and pick a network](docs/recipes/scan-and-pick-a-network.md),
  [access point fallback](docs/recipes/access-point-fallback.md).

## License

MIT.
