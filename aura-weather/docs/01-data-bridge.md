# 01 - Data bridge (phone-side JavaScript)

## Problem

The watch cannot make network calls, hold a large table, or parse a forecast. All of that runs in the PebbleKit JS sandbox on the phone. The bridge fetches from the right provider for the location, normalises everything to one small code set and one small struct, and ships it to the watch in size-limited AppMessages. This is the port of `AEMETClient.swift` plus the new worldwide provider, minus everything the watch does not need.

## Files

- `src/pkjs/index.js`: entry point, `ready`/`appmessage` handlers, refresh scheduler, AppMessage send. Model on `../aura-analog/src/pkjs/index.js` (same `XMLHttpRequest`, 15s timeout, `clay-settings` read, GPS fallback), extended from one field to the full forecast.
- `src/pkjs/providers.js`: `fetchOpenMeteo(lat, lon)`, `fetchAEMET(ine, apiKey)`, each returning the same normalised object. New file.
- `src/pkjs/normalise.js`: `wmoToCode()`, `aemetToCode()`, unit conversion, the compact packer. New file.
- `src/c/*.c` inbox handler: reads the keys below, fills the `Weather` struct, persists it, marks the screen dirty.

## Provider routing

`chooseProvider(location)`:

- If the location carries an INE code (it is Spanish, resolved in `02-location-search.md`) **and** an AEMET key is set **and** the AEMET path is enabled, use `fetchAEMET(ine, key)`.
- Otherwise use the keyless worldwide provider (`fetchOpenMeteo` under recommendation A/B; see the decision in `00`).

The two functions return the identical shape, so `index.js` never branches on provider past this point.

### Open-Meteo (keyless, worldwide)

One call gives current, hourly and daily plus sun times:

```
https://api.open-meteo.com/v1/forecast
  ?latitude=<lat>&longitude=<lon>
  &current=temperature_2m,relative_humidity_2m,weather_code,precipitation_probability
  &hourly=temperature_2m,weather_code,precipitation_probability
  &daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset
  &forecast_days=6&timezone=auto
  [&temperature_unit=fahrenheit]
```

`weather_code` is WMO, mapped by `wmoToCode()`. `sunrise`/`sunset` come back as ISO local strings; convert to unix seconds for today. This is the same endpoint family the analog face already uses, widened.

### AEMET (Spain, key required)

Two-call model, exactly as `AEMETClient.swift`. Daily and hourly are separate endpoints:

```
GET https://opendata.aemet.es/opendata/api/prediccion/especifica/municipio/diaria/<ine5>?api_key=<KEY>
GET https://opendata.aemet.es/opendata/api/prediccion/especifica/municipio/horaria/<ine5>?api_key=<KEY>
```

Each returns `{ datos: "<url>", estado: 200 }`; fetch `datos` for the payload. Watch for: `estado` 429 (rate limited, back off and reuse the cached struct), ISO-8859-1 body (decode, do not assume UTF-8), and the coarse daily blocks (`00-24`, `00-12`, ...). Collapse to one representative daily value the way the app does: daily precip = max across blocks, daily sky = the daytime block. Condition strings like `"11"`, `"13n"` map through `aemetToCode()`. AEMET does not return sun times; compute sunrise/sunset from lat/lon with the same NOAA maths the watch uses (`03`), or let the watch compute them (cheaper: send lat/lon once and let the watch do it).

## Normalisation to the internal code set

The watch only understands the 10-value enum from `00`. Two lookup tables:

`wmoToCode(w)`:

```
0 -> 0            (clear)
1,2 -> 1          (few clouds)
3 -> 3            (overcast)   // note: WMO 2 is "partly", 3 is "overcast"
45,48 -> 4        (fog)
51,53,55,56,57 -> 5 (drizzle)
61,63,80,81 -> 6  (rain)
65,66,67,82 -> 7  (heavy rain)
71,73,75,77,85,86 -> 8 (snow)
95,96,99 -> 9     (thunder)
default -> 1
```

`aemetToCode(s)` (strip a trailing `n`, then match the numeric part):

```
11 -> 0
12,13 -> 1
14,15 -> 2
16,17 -> 3
81,82 -> 4        (niebla / bruma)
23,43 -> 5
24,25,44,45,26,46 -> 6/7 (24/44 rain, 25/26/45/46 heavy)
33,34,35,36,71,72,73,74 -> 8 (snow)
51,52,53,54,61,62,63,64 -> 9 (storm)
default -> 1
```

Units: convert once, on the phone, per `settings.UNITS`. The watch stores whole-degree ints and never converts.

## AppMessage protocol

AppMessage payloads are small and the inbox buffer is finite, so the forecast goes in **framed chunks**, not one giant dictionary. Keep each message well under the negotiated inbox size (size it explicitly in `app_message_open`, aim for a 512-byte-ish inbox and send comfortably inside it).

Message keys (declare in `package.json`, include `message_keys.auto.h` in C):

- Handshake / current: `WX_OK` (1 good, 0 error), `WX_NAME` (cstring), `WX_TEMP`, `WX_TMIN`, `WX_TMAX`, `WX_CODE`, `WX_HUM`, `WX_POP`, `WX_SUNRISE`, `WX_SUNSET`, `WX_UNITS`, `WX_UPDATED`.
- Hourly frame: `H_IDX` (0..7), `H_TEMP`, `H_CODE`, `H_POP`. One message per hour slot.
- Daily frame: `D_IDX` (0..5), `D_MIN`, `D_MAX`, `D_CODE`, `D_POP`. One message per day slot.
- Settings echoed from config: `UNITS`, `LOCMODE`, `LAT`, `LON`, `INE` (cstring, empty if non-Spanish), `AEMET_KEY` (cstring), `USE_AEMET` (bool).

Send order: one current message, then the 8 hourly frames, then the 6 daily frames, each only after the previous `outbox_sent` callback fires (chain them, do not fire 15 at once, PebbleKit JS drops overlapping sends). The watch fills its struct by `*_IDX` and re-renders when the last daily frame lands or on a short debounce. On any provider error send `{ WX_OK: 0 }` and keep showing the persisted struct.

## Refresh cadence

On `ready`, then `setInterval(refresh, 30 * 60 * 1000)`. `SELECT` on the watch sends an outbound "refresh now" message the JS listens for. Reuse the analog `LOCMODE`/`LAT`/`LON`/GPS logic verbatim for where to fetch.

## Constraint

One normalised shape out of every provider. The watch must be unable to tell which provider answered. Never send a raw AEMET or WMO code to the watch.

## Edge cases

- (a) AEMET 429 or timeout: back off, keep the cached struct, do not blank the screen.
- (b) Non-Spanish location with the AEMET path enabled: ignore the key, use the keyless provider (no INE to query).
- (c) Fahrenheit: convert on the phone; the watch is unit-agnostic and only paints `WX_UNITS` for the degree label.
- (d) Overlapping sends: strictly chain frames on `outbox_sent`; a dropped frame just leaves a stale slot until the next refresh, which is acceptable.

## Done

- Both providers return byte-identical normalised objects and the watch renders the same from either.
- A full forecast (current + 8 hours + 6 days + sun times) arrives and survives a watch reboot via persist.
- Turning the phone off still shows the last forecast with its `updated` age.
