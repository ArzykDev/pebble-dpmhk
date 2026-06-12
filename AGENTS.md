# MHD HK — Pebble departures watchapp for Hradec Králové

Pebble (repebble SDK 3) watchapp showing live DPMHK bus/trolleybus departures.
C app on the watch renders; PebbleKit JS (`src/pkjs/`) on the phone does all
networking, GPS, and caching. Czech UI, system fonts (new PebbleOS firmware
renders Czech diacritics natively — do NOT add custom fonts for this).

## Build / run / debug

The Pebble emulator has broken deps on the Fedora host — run **all** pebble
commands inside the `ubuntu` distrobox (shares $HOME; the
`LD_PRELOAD libnxegl.so` warning there is harmless):

```sh
distrobox enter ubuntu -- pebble build
distrobox enter ubuntu -- pebble install --emulator basalt   # any of the 7 platforms
distrobox enter ubuntu -- pebble logs                        # APP_LOG + JS console.log
distrobox enter ubuntu -- pebble screenshot
distrobox enter ubuntu -- pebble emu-button click down       # drive UI headlessly
distrobox enter ubuntu -- pebble kill                        # stop emulators
```

After editing `messageKeys` in package.json run `pebble clean` once — the
generated `message_keys.auto.*` can go stale. Real watch:
`pebble install --cloudpebble` (needs Dev Connect + `pebble login`). The
emulator runs PKJS via pypkjs: XHR works; geolocation is IP-based (GeoLite),
so "nearest stops" rank against your IP location, not HK.
Do NOT use `pebble emu-bt-connection --connected no` to test offline — it cuts
pebble-tool's own control socket and you must `pebble kill` to recover.

## CI & releasing

GitHub Actions (`.github/workflows/pebble.yml`) builds all 7 platforms on every
PR and push to `main` — a compile gate only (no test suite). `make build` wraps
`pebble build`; on the Fedora host pass the box:
`make build PEBBLE="distrobox enter ubuntu -- pebble"`.

Versioning is SemVer `MAJOR.MINOR.PATCH`, kept in `package.json` `version` and
surfaced as the `.pbw` `versionLabel` (all three parts are preserved).
PATCH = fixes/strings, MINOR = new feature, MAJOR = redesign or a change that
breaks persisted state or the AppMessage contract. Releases are deliberate, not
per-commit: bump `version`, commit (`chore(release): vX.Y.Z`), then push tag
`vX.Y.Z`. The tag must equal `version` (CI enforces); the tagged build publishes
a GitHub Release with the `.pbw` attached.

## Platforms

`aplite basalt chalk diorite emery flint gabbro`. Constraints: aplite = 24 KB
app RAM (smallest; AppMessage inbox kept at 512 B there, 1024 B elsewhere —
see `src/c/comm.c`); chalk/gabbro are ROUND (`menu_layer_set_center_focused`,
content margin `PBL_IF_ROUND_ELSE(18, 4)`); emery 200×228. Never hardcode
144×168.

## Data source: api.dpmhk.cz (undocumented official backend)

No auth, open CORS, JSON UTF-8. Contracts live ONLY in `src/pkjs/api.js`.

- `GET /packet` → `[{packet:"185", from:"2026-06-01", to:"2026-06-08"}, …]`
  (weekly timetable packets; pick the one covering the query date)
- `POST /stations` `{"packet":"185"}` → 204 stops `{id,name,lat,lng,linky[]}`
- `POST /odjezd` `{"packet":"185","zastavka":"2","datum":"06_06_2026"}` →
  `[{linka,odjezd,smer,delay_seconds,delay_status}, …]`
- `POST /trasa` `{"packet":"185","linka":"16","smer":"1"}` →
  `[{order:"6",name:"STĚŽÍRKY"}, …]` ordered stops of a line. **`smer` is a
  `0`/`1` direction code, NOT the destination text**; rows come in travel order
  (smer=0 = ascending `order`, smer=1 = descending). `order` is a global stop
  index shared across both directions, so it pins direction even when a
  destination stop is absent from the chosen array. **No per-stop times** —
  names only (powers the trip-route screen).

Gotchas: **`datum` is `DD_MM_YYYY` with underscores**; `linka` is space-padded
(trim it); `/odjezd` returns a near-future window from server "now" (2–18
rows). The API is undocumented and may change — parse defensively, fall back
to cache, keep request volume low.

## Layout

```
src/c/pebble-dpmhk.c   main()
src/c/comm.c           ALL app_message_* usage; request/response protocol
src/c/model.c|h        Departure/StopRef/DepartureBoard structs + static stores
src/c/persist.c        watch storage: favorites mirror + last board (≤256 B/key)
src/c/strings.h        every Czech UI string (UTF-8 literals)
src/c/windows/         stops_window (Oblíbené+Nejbližší), departures_window,
                       trip_window (downstream stops of a tapped departure)
src/pkjs/index.js      'ready'/'appmessage' router + Clay wiring
src/pkjs/appmsg.js     header + chained row messages (protocol mirror of comm.c)
src/pkjs/api.js        XHR client for api.dpmhk.cz (the only file knowing URLs)
src/pkjs/departures.js packet pick → /odjezd → 23:00 next-day merge
src/pkjs/trip.js       packet pick → /trasa both dirs → downstream stop slice
src/pkjs/cache.js      localStorage: stations:list, cfg:favorites, dep:<id>
src/pkjs/geo.js        geolocation + haversine top-5 nearest
src/pkjs/config.js     Clay config items (6 favorite inputs)
src/pkjs/custom-clay.js runs inside the config webview; injects stop datalist
src/pkjs/util.js       diacritics fold (NO localeCompare — pypkjs lacks ICU!)
src/pkjs/date.js       DD_MM_YYYY datum, packet pick by range
```

## AppMessage protocol

Request (watch→JS): `{OP, REQUEST_ID, STOP_ID?, META_STOP_NAME?}`
(GET_DEPARTURES sends META_STOP_NAME so the phone re-resolves the
packet-scoped stop id by name — see Caching). OPs: 1=GET_DEPARTURES,
2=GET_NEAREST, 3=GET_TRIP, 4=FAVORITES (JS→watch push, REQUEST_ID=0),
5=ADD_FAVORITE, 6=REMOVE_FAVORITE (watch long-press; JS answers with an OP=4
push). GET_TRIP reuses request row keys: ROW_LINE=line, ROW_DEST=destination
text, META_STOP_NAME=current stop; the phone resolves the /trasa direction by
name and streams downstream stop names back (rows: ROW_LINE=stop name).
OPs 5/6 are fire-and-forget: sent with REQUEST_ID=0 and they must NOT bump
the request counter — bumping it strands in-flight tracked replies (their
rows fail the stale-id check and loading flags never clear).
Response (JS→watch): one header `{REQUEST_ID, OP, META_STOP_NAME?, META_COUNT,
META_FLAGS, META_FETCHED_AT, ERROR}` then one message per row
`{REQUEST_ID, OP, ROW_INDEX, ROW_LINE, ROW_DEST, ROW_TIME, ROW_DELAY}`,
chained on send-success. Stops/favorites rows reuse keys: ROW_LINE=name,
ROW_META=id, ROW_TIME=distance string. `REQUEST_ID` echoed everywhere; C drops
mismatches. Omitted META_STOP_NAME keeps the watch's request-time name.
`ROW_DELAY` −32768 = unknown. META_FLAGS: bit0 cached, bit1 stale. ERROR:
1 network, 2 API, 3 parse, 4 no packet, 5 GPS. Keep `comm.c`, `appmsg.js`, and
`model.h` enums in sync when changing any of this.

## Caching (live + offline fallback)

Departure boards are NEVER cached on the phone: a board is a snapshot of the
next few minutes, so any replayed copy shows already-departed buses and stale
delays. The online path always fetches fresh — the watch shows "Načítám..."
until live data lands (no stale-while-revalidate flicker). Phone
(localStorage) caches only `stations:list` (refreshed when the weekly `/packet`
id changes) and `cfg:favorites`.

Offline fallback lives entirely on the watch (persist): key 1 = favorites
mirror (instant first paint), key 2 = last board (4 rows, fresh boards only).
The persisted board is shown with the Offline badge whenever live data can't be
delivered — both when the phone is unreachable (`prv_departures_send_failed`,
send failure) AND when the phone reports a fetch error (network/API/parse/
no-packet; `prv_handle_departures_header` calls `persist_load_board`). The bare
error screen appears only when no board is stored for that stop.

## Clay config page

6 text inputs + one shared `<datalist>` of all 204 stop names (typeahead).
The names travel via `clay.meta.userData` — NOT as select options (6×204
options once made a 257 KB URL that broke webviews). With
`autoHandleEvents: false`, Clay never populates `clay.meta` itself — index.js
builds it by hand in `showConfiguration`. Inputs hold stop NAMES; index.js
maps name→id on `webviewclosed` (fold diacritics via util.js for matching).
Clay must be constructed with the real config (components register in the
constructor — `new Clay([])` renders an empty page).

## Conventions

- C: SDK style — `prv_` static functions, `s_` statics, 2-space indent.
- JS: classic PKJS — XHR only (no fetch), no localeCompare, no Promises.
- Czech strings only in `strings.h` (C) — never inline in windows.
- SDK docs: trust `developer.repebble.com` over the older `developer.rebble.io`
  — the latter still carries stale pre-repebble guidance.
- Commits: Conventional Commits (feat:, fix:, chore:, ...), 50/72, plain text
  (no markdown in messages).
- Agent-filed GitHub issues: add the `claude` label and a
  `🤖 Drafted by Claude Code` footer. `gh` authenticates as the repo owner, so
  the issue is attributed to the user regardless — the label/footer is the only
  marker distinguishing agent-drafted issues from the owner's own.
- Do not touch: `build/` (generated), `wscript` (works as-is).
