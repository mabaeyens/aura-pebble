# 00 - Portability and architecture

## The honest starting point

Aura on iOS and Android is **Spain-only**. AEMET OpenData is the only forecast source, everything is keyed by the 5-digit INE municipality code, and there is no geocoder and no worldwide provider anywhere in the source. So three of my five must-haves are genuinely new work, not a port:

1. Worldwide coverage (a second provider).
2. Worldwide city search that returns the closest matches.
3. Routing a Spanish result back to an AEMET municipality so Spain still gets AEMET.

That reframing matters because it tells me where the risk is. The pretty parts (hero, sun over the sky) are cheap. The location and provider plumbing is where the real design work sits.

## What ports cleanly

- **The sun-over-day/night background.** This is the standout. In the app it is the rarest asset (only the two conditionless `wide_*_day` bases and their night twins let the live sun move over them), and on Pebble it becomes the easiest thing to reproduce because it needs no art at all. `AuraSunPath` and `SolarTimes` in `AuraSky.swift` are pure arithmetic: NOAA sunrise/sunset, then `f = (now - sunrise) / (sunset - sunrise)`, `x = f`, `altitude = sin(f * pi)`, `y = 0.80 - altitude * 0.66`. That re-implements in a few dozen lines of C with no dependencies. The Sunset watchface already proves a gradient sky with a sun following the real solar path renders fine on Emery.
- **The Aura colour identity.** Aura has no logo, the data is the colour. `docs/PALETTE.md` already re-encodes the temperature ramp to the 64-colour Emery palette with the snap rule (`level = round(v * 3) * 85`). I reuse it verbatim.
- **Condition to icon.** A small switch over a handful of codes. The analog face already bundles the Weather Icons font and maps codes to glyphs; I reuse that font and extend the switch.
- **The hero, hours and days as concepts.** The layouts collapse to a watch, but the information design (big current temp with hi/lo, a horizontal hourly strip, a short daily list) is the same idea at a smaller size.
- **The two-call AEMET client shape.** The envelope-then-`datos` fetch, the `api_key` param and the ISO-8859-1 decode all move to the phone-side JavaScript almost unchanged.

## What does not port

- **The ~280 baked hero images (884 imagesets across targets).** Wrong resolution, far too big, and unnecessary once the sky is procedural. Drop all of them; draw the sky in code.
- **SF Symbols.** Replaced by the bundled Weather Icons font (already in use) or drawn glyphs.
- **Frosted-glass cards, gradients-behind-blur, the whole SwiftUI layer.** No blur on Pebble. Flat blocks and drawn shapes only. Zero code shared, by design, exactly as the watchfaces already do.
- **AEMET observations, nearest-station logic, radar images, CAP warning `.tar` parsing, regional bulletins.** Too heavy for the watch and low value at a glance. Out of scope for v1. A one-line active-warning banner could come back later as text only.
- **Air quality, moon phase, golden/blue hour, feels-like, UV-per-hour.** Nice-to-have, all deferred. Touchy Weather ships them, but they are scope, not necessity.

## The one decision I need from you (worldwide provider)

The must-have says AEMET for Spain and OpenWeather for the rest. The catch is that **both** AEMET and OpenWeather need a per-user API key (AEMET a free long JWT by email, OpenWeather a registered key with a free tier), and asking every watch owner to paste two keys into settings is the single biggest adoption killer on this platform. The existing bridge already proves that **Open-Meteo works with no key at all**, and it covers Spain and the world.

My recommendation, in order of how much I would fight for it:

- **A) Keyless everywhere for v1 (Open-Meteo worldwide), AEMET optional.** Ship with Open-Meteo for all locations so the app just works on install. Add an optional "use my AEMET key for Spain" toggle in settings for people who want the official Spanish source and its warnings later. Lowest friction, fastest to a shippable v1, keeps the Aura-in-Spain story available without forcing it on everyone.
- **B) AEMET for Spain (key required), Open-Meteo for the rest (keyless).** Honours "AEMET for Spain" literally, still no key for the rest of the world. Cost: Spanish users must register an AEMET key before the app shows anything, which is a rough first run.
- **C) AEMET for Spain, OpenWeather for the rest (both keys).** What the must-have literally says. Two keys, worst first run, and OpenWeather gives me nothing Open-Meteo does not for this feature set. I would not pick this.

I have written the data-bridge spec around a **provider-agnostic normalisation layer** so this choice only swaps a fetch function, not the protocol. Tell me A, B or C and I finalise `01-data-bridge.md`. Until then I am building toward A with AEMET wired but off by default.

## App architecture

This is a watchapp, so it owns the whole screen and the buttons.

**Window flow.** One `Window` with a screen index, three screens the user pages through, plus a settings hint. Buttons are the baseline (every Pebble has them); touch on the Time 2 is a later enhancement, never required.

- `UP` / `DOWN`: move between the three screens, Hero -> Hourly -> Daily and back.
- `SELECT`: force a weather refresh (also auto-refreshes on launch and every 30 min).
- `BACK`: exit to the launcher.
- All configuration (location, units, provider key) lives in the phone Clay settings, not on the watch.

**Three screens.**

1. **Hero** (`03-hero-and-sun-background.md`): the sun/moon over the day/night sky, current temp, condition glyph, hi/lo, location name.
2. **Hourly** (`04-hours-and-days.md`): a vertical or horizontal strip of the next hours (hour, glyph, temp, precip%).
3. **Daily** (`04-hours-and-days.md`): the next days (weekday, glyph, min/max).

**On-watch data model.** The watch never parses a forecast. The phone sends a compact, already-digested struct. Keep everything as small integers.

```c
typedef struct { int8_t temp; uint8_t code; uint8_t pop; } HourSlot;   // pop = precip probability %
typedef struct { int8_t min; int8_t max; uint8_t code; uint8_t pop; } DaySlot;
typedef struct {
    char     name[24];        // location label, e.g. "Madrid"
    int8_t   temp;            // current temp, whole degrees
    int8_t   tmin, tmax;      // today hi/lo
    uint8_t  code;            // normalised condition (see 01-data-bridge)
    uint8_t  humidity;        // %
    uint8_t  pop;             // current precip probability %
    int32_t  sunrise, sunset; // unix seconds, today, watch draws the sun from these
    uint8_t  is_metric;       // 0 = F, 1 = C
    HourSlot hours[HOURS_N];  // HOURS_N = 8
    DaySlot  days[DAYS_N];    // DAYS_N = 6
    int32_t  updated;         // unix seconds of the fetch
} Weather;
```

Persist the whole struct with `persist_write_data` so the last forecast shows instantly on launch, before the phone answers. `sunrise`/`sunset` come from the phone so the sun is correct even for a location the watch is not physically at.

## Condition code set

The app uses AEMET "estado del cielo" strings, the existing bridge uses WMO integers, OpenWeather uses its own. Rather than teach the watch three taxonomies, the **phone normalises to one tiny internal set** before sending. This lives in `01-data-bridge.md`. The watch only ever sees this enum:

```
0 clear    1 few-clouds   2 cloudy    3 overcast   4 fog
5 drizzle  6 rain         7 heavy-rain 8 snow      9 thunder
```

A separate `is_night` is not sent; the watch derives night from `now` versus `sunrise`/`sunset` and picks the night glyph itself, exactly as the app derives it from real sun times rather than the code suffix.

## Project scaffold

Model the project directory on `aura-essential` (single C file grows into a few, multi-JS pkjs, Clay). Concrete settings:

- `uuid`: `fce83b9e-37bc-4714-9125-840d83a72d64` (minted for this app).
- `pebble.watchapp.watchface`: **false**. This is an app, not a face. This is the one setting `pebble new-project` gets right for us and the faces had to flip.
- `targetPlatforms`: `["aplite","basalt","chalk","diorite","emery"]`. Emery (Time 2) is the design target at 200x228 and 64 colours; lay out from `layer_get_bounds()` so `chalk` (round) and the 144x168 legacy platforms still render.
- `capabilities`: `["configurable","location"]`. No `health` (a weather app needs no step count).
- `enableMultiJS`: true, `sdkVersion`: "3".
- Fonts: bundle `WeatherIcons-Regular.ttf` (condition glyphs, character-subset) and reuse Liberation Sans Bold for text, both already living in `../aura-analog/resources/fonts/`.
- Message keys: see `01-data-bridge.md` for the full list; always `#include "message_keys.auto.h"`, never hand-declare the keys extern (that breaks the older pebble-tool CloudPebble runs).

## Build order (suggested)

1. Scaffold the app, hero screen with hard-coded data, sun-path maths. Proves the signature on Emery early.
2. Data bridge with one keyless provider (Open-Meteo), single-location manual lat/lon. Proves live data end to end.
3. Hourly and daily screens fed by the chunked AppMessage.
4. Location search in settings (worldwide typeahead, Spain-to-INE resolution).
5. AEMET path behind the settings toggle. Depends on the worldwide-provider decision above.
