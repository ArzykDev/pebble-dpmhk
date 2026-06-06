# MHD HK

Pebble watchapp showing live bus and trolleybus departures for Hradec
Králové (DPMHK). Open the app and see the next departures (line,
destination, time, realtime delay when available) from your favorite stops
or the stops nearest to you.

## Features

- **Odjezdy** — live departure board per stop, fed by the (undocumented)
  official `api.dpmhk.cz` backend that powers dpmhk.cz's own search
- **Oblíbené** — favorite stops, configured on the phone (settings page
  with stop-name typeahead) or directly on the watch (long-press a nearby
  stop to add, long-press a favorite to remove)
- **Nejbližší** — nearest stops via phone GPS
- **Offline** — last results are cached on the phone and watch; the board
  shows `Offline (uloženo HH:MM)` when live data is unreachable
- Czech UI, all 7 Pebble platforms (aplite → gabbro), round-display aware

## Building & running

Requires the [repebble SDK](https://developer.repebble.com/sdk/)
(`pebble-tool`):

```sh
pebble build                          # build for all targetPlatforms
pebble install --emulator basalt     # run in the emulator
pebble install --cloudpebble         # install to a real watch via Dev Connect
```

See [AGENTS.md](AGENTS.md) for architecture, the AppMessage protocol, API
contracts, and development gotchas.

## Data source

Static + realtime departure data comes from `api.dpmhk.cz`, the JSON backend
of [dpmhk.cz](https://dpmhk.cz)'s timetable search. It is undocumented and
unaffiliated with this project; the app caches aggressively and degrades to
cached data when it is unavailable. This app is not an official DPMHK
product.
