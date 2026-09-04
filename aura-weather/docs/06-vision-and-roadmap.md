# 06 - Vision: the complete Aura Weather

This doc sets the direction for Aura Weather on the Pebble Time 2. It aligns the best existing Pebble weather apps with what Aura already is on iOS, and lays out how Aura becomes more complete than even Touchy Weather while keeping a soul of its own. It is a strategy doc, not a spec; the numbered specs (`00`..`05`) still govern what is built.

## The one-line thesis

Aura is the only weather app on Pebble where the value is the colour, the sky is alive, and the forecast is real Spanish meteorology (AEMET, MITECO, Copernicus) with the official bulletins and avisos nobody else carries. Touchy Weather wins today on breadth and configurability against generic providers; Aura can match that breadth and then win on identity, because Aura already has a full iOS, macOS and watchOS app whose design language and data model I can borrow wholesale.

## What each reference app teaches

| App | The one thing it does best | What Aura takes |
| --- | --- | --- |
| Weather (Flo Knodl) | Restraint plus beautifully drawn icons; current plus 3-day and nothing more | The default screen must be calm and gorgeous before it is deep. Aura already has real vector Weather Icons glyphs; the lesson is to keep the first glance uncluttered. |
| Simple Weather (Cutman) | Its app icon | Give Aura the same treatment: the real Aura icon at 144x144, not a placeholder. See "The icon". |
| Weather (DocF87) | Readability and a clear structure; select toggles hourly/daily, down toggles precip/wind | A legible, well-structured screen set and an obvious, learnable button and gesture model. |
| Touchy Weather | The completeness bar: card-based, swipeable, pull-to-refresh, and a user-configurable card layout over a deep data set (feels-like, wind, humidity, hourly, 4-day, precip charts, UV, AQI, sun times, moon phase, golden/blue hour, radar) | The card architecture, the configurable layout, the touch model, and the feature checklist to reach and pass. |
| Aura iOS/watchOS | A coherent design language and data model across every surface | Everything structural: the colour ramps, the complication gauge vocabulary, the card stack, the `WeatherSnapshot` fields, and the bulletin and aviso system. This is the source of truth, not a competitor. |

## Aura's soul: what must never be lost

These four things are why Aura is Aura. Every feature below serves them; none may dilute them.

1. Data as colour. The value is the colour. Temperature, wind, UV, air quality and precipitation each have a ramp (`docs/PALETTE.md`, mirrored from `aura-apps/Sources/AuraKit/Palette.swift`). A number and its gauge are always the same colour. This is the identity, not a logo.
2. The living sky. The hero draws a real sun and moon path over a procedural day/night sky and landscape with cast shadows, computed from actual sun times, with no baked art. It is already built and it is the signature.
3. Real meteorology, tiered by place. Everywhere on Earth, Open-Meteo gives Aura the numbers, and Aura turns them into a plain-language summary and threshold advisories on the phone, so every user gets a narrative and a warning, not just a grid of figures. In Spain, AEMET and MITECO enrich that with the official layer: real avisos in the four colour levels, the regional and national boletines, the nearest observing station, the ICA air-quality scale. Touchy shows numbers from a generic provider and nothing more; Aura gives everyone words and warnings, and gives Spain the official forecast on top.
4. One design language everywhere. The same ramps and the same card ideas render on the phone, the widget, the Apple Watch complication, and now the Pebble. A Pebble screen should feel like the same product as the iOS hero, shrunk and redrawn, never a separate lesser app.

Calm by default, deep on demand. The first screen is a quiet, beautiful hero. The depth is one swipe away, never in your face. This is the line that separates Aura's voice from Touchy's jokey "Touch and Go" advice card, which I deliberately do not copy: Aura's voice is the editorial, AEMET-grounded Spanish prose the iOS app already generates, calm and true, not a gimmick.

## Providers and reach: Open-Meteo everywhere, AEMET in Spain

The phone and Android apps are proudly Spain-only on AEMET, and they stay that way. The Pebble app is a different bet. The Spanish Pebble market is small, so on the watch the priority is reach, and reach means Open-Meteo: free, worldwide, no account, and good enough numbers for anyone. AEMET then rides on top for the wearers who are in Spain, inland or in the islands, and who paste a free AEMET key. Open-Meteo is the floor everyone stands on; AEMET is the enrichment for those who can use it.

Provider rule of thumb: any source is welcome as long as it is free, even if it needs a free key. Open-Meteo needs none. AEMET needs a free key, which is fine. If a better free source appears for a region, it slots into the same tiered model.

Open-Meteo is numbers only. It has no prose bulletin and no warnings endpoint, confirmed in the project's own tracker and docs. That is not a gap for Aura, it is an opening, because Aura already knows how to make words and warnings out of numbers:

- Prose for everyone. Aura iOS writes its own editorial summary from the forecast (`ForecastPhrase`). The phone can do the same for the Pebble from Open-Meteo data, so a wearer in Berlin or Buenos Aires gets a real plain-language line, not a blank card. In Spain, that generated line is replaced by, or shown alongside, AEMET's official regional and national boletin. The bulletin card is therefore universal, generated internationally and official in Spain, not a Spain-only feature as an earlier draft assumed.
- Warnings for everyone. The watch already derives a coarse advisory from the condition (`wx_warning` gives STORM, HEAVY RAIN, SNOW, FOG). Extend that on the phone with thresholds on wind, precipitation, heat and cold, and every wearer gets a glanceable warning worldwide. In Spain, AEMET's official CAP avisos with their four colour levels replace the derived advisory, because an official aviso always outranks a guess.
- Official warnings beyond Spain, later and still free. MeteoAlarm covers Europe and weather.gov covers the United States, both free. They can layer in region by region as a later phase, upgrading the derived advisory to an official one wherever a free feed exists, exactly as AEMET does for Spain.

So the tiering is: Open-Meteo numbers as the worldwide floor, Aura-generated prose and advisories so nobody sees an empty card, and official national feeds (AEMET now, MeteoAlarm and weather.gov later) as the enrichment where they exist and are free.

## The gauge vocabulary borrowed from the Apple Watch complications

Aura iOS ships 15 complications, and their layout rules are the perfect grammar for small Pebble cards. The rule set:

- A bounded metric (humidity 0-100, rain probability 0-100, UV 0-to-peak, ICA 1-6) is drawn as a radial ring or an arc gauge, filled in its ramp colour.
- An unbounded metric (temperature) is drawn as coloured text, no gauge, because there is no full scale to fill.
- The primary value sits large and centred; the scale, the range, or the "time until" rides the outside as a secondary reading.
- The colour of the value and the colour of its gauge are always the same ramp colour.

Every Aura Pebble card follows this grammar, so the whole app reads as one system.

## The card architecture: how Aura reaches and passes Touchy

Aura Weather becomes a stack of cards, mirroring the iOS `AuraForecastStack`, paged one at a time. This is where Aura matches Touchy's breadth and configurability, then adds what Touchy lacks.

Card catalogue, in the natural default order (each maps to an iOS card or complication):

1. Hero. Current temp, condition, hi/lo, location, over the living sky and landscape. Built.
2. Aviso. A colour-coded warning card. Worldwide it shows Aura's own threshold advisory (storm, heavy rain, snow, wind, heat, cold, fog) derived from the Open-Meteo numbers. In Spain it is upgraded to the official AEMET aviso in the four colour levels (verde/amarillo/naranja/rojo) with the one-word phenomenon label (Calor, Tormentas, Nieve, Viento, Niebla, Calima, Costa, Frio), exactly as the iOS `AuraAlertCard` and `AuraAviso` complication behave. Shown only when a warning is active.
3. Hourly. The next hours as ramp-tinted trend dots on a shared min-to-max track. Built.
4. Daily. The next days with ramp-tinted min/max. Built.
5. Sun and Moon. A daylight-remaining arc with sunrise and sunset at the ends by day, and a drawn moon-phase disc with the phase name after dark. Mirrors `AuraSunRect` and `AuraSunMoon`.
6. Wind. A compass needle over a rose, coloured by the wind ramp, with speed and direction, tapping through to a Beaufort scale sheet. Mirrors `AuraWindNeedle`.
7. UV. A ring from 0 to today's peak in the WHO colour, the live index, and the band name (Bajo, Moderado, Alto, Muy alto, Extremo). Mirrors `AuraUV`.
8. Air quality. The ICA 1-6 ring in the official MITECO colour with the category name, shown only when a station is near. Mirrors `AuraAirQuality`.
9. Details. Feels-like, humidity, and precipitation amount, the fields the hero omits to stay calm.
10. Bulletin. A scrollable plain-language forecast. Worldwide it is Aura's own generated summary from the Open-Meteo numbers, so no wearer sees an empty card. In Spain it is the official AEMET regional community boletin, with the national forecast behind it, mirroring `AuraBulletinCard` and `ForecastTextView`. Universal, generated internationally and official in Spain.
11. Station. The nearest real AEMET observation with its distance, gated on freshness. Mirrors `AuraStationCard`. Later.
12. Radar. A single small regional radar tile. Later, and only if the AppMessage and memory budget allows (see "Pebble reality checks").

Two behaviours combined:

- Cards appear and disappear by data availability, the iOS rule. No air-quality station near you, no air-quality card. No active aviso, no aviso card.
- Cards are user-orderable and toggleable from settings, the Touchy rule that the iOS app does not have. On Pebble I can actually go one better than Aura iOS here: let the wearer choose which cards they see and in what order, and which card the app boots on.

## Navigation and interaction

The Pebble Time 2 has a touch screen and buttons, so support both, the way DocF87 uses buttons and Touchy uses swipes.

- Boot on the hero (or on the wearer's chosen default card).
- Up and down buttons, or swipe up and down, page through the visible cards.
- Select forces a refresh; a swipe-down-at-top pull-to-refresh mirrors Touchy.
- A tap on a card with a scale (wind, UV, air quality) opens its reference sheet (Beaufort, WHO bands, ICA levels), mirroring the iOS `AuraScaleSheets`.
- Back exits.
- A long press opens settings for card order, visibility, location and units.

## Data model extensions

The current on-watch `Weather` struct (`src/c/weather.h`) carries name, temp, hi/lo, code, humidity, pop, sun times, units, and the hours and days arrays. To feed the cards above it needs the fields the iOS `WeatherSnapshot` already has. The watch stays dumb: the phone digests, the watch tints. Additions:

- Current conditions: `feels_like` (int8), `wind_speed` (uint8 km/h), `wind_dir` (uint8, 16-point index 0-15), `wind_gust` (uint8), `precip_mm` (uint8, tenths or whole), `storm_prob` (uint8 %).
- Environment: `uv` (uint8) and `uv_peak` (uint8) for the ring, `aqi` (uint8, ICA category 1-6, 0 = none/no station near).
- Astronomy: `moon_phase` (uint8, 0-7 principal phase, computed on the phone from `MoonPhaseMath`) and `moon_illum` (uint8 %).
- Aviso: `alert_level` (uint8, 0 none / 1 verde / 2 amarillo / 3 naranja / 4 rojo) and a short `alert_label` (a small string or an enum index into a fixed one-word table). The phone fills these from its own thresholds worldwide, or from the official AEMET CAP aviso in Spain; the watch does not know or care which produced them.
- Bulletin: `bulletin` text (chunked AppMessage string) plus `bulletin_phenomenon`. The phone generates it from the numbers worldwide, or sends AEMET's official boletin in Spain. This is the largest single payload; send it last and chunked, regional first, national on demand.
- Optionally per-slot wind in `HourSlot` and `DaySlot`, since the iOS hours and days carry `windSpeed`.

Message keys continue from the base-10000 block already in use; group the new current-condition keys, the environment keys, the aviso keys, and the bulletin chunk keys into contiguous ranges. Adding `messageKeys` requires a `pebble clean` before rebuild (the `message_keys.auto.*` cache), the same gotcha the analog face hit.

The phone side (`src/pkjs/index.js`) already normalises Open-Meteo and can route AEMET in Spain. Wind, UV and precipitation come straight from Open-Meteo worldwide. The generated bulletin and the derived aviso are produced on the phone from those same numbers, so they work everywhere. Only the ICA air quality and the nearest observing station stay Spain-only, because MITECO and the AEMET station network have no worldwide equivalent; and in Spain the generated bulletin and derived aviso are replaced by AEMET's official boletin and CAP aviso. That is the tiered model in code: Open-Meteo fills every field for everyone, AEMET overwrites the ones it can do better for Spain.

## Settings (Clay) additions

The config screen already drives location, units and the optional AEMET key. Add:

- Card visibility and order (the configurable layout).
- The default card to boot on.
- Keep it short; the wearer sets it once.

## The icon

Aura has a real icon and it should be on the watch. The source is `aura-apps/Aura/Assets.xcassets/AppIcon.appiconset/icon-1024.png` (a light and a dark 1024x1024 raster; there is no vector). It is a rounded white capital A on a blue gradient with a warm yellow sun-glow in the top-left corner and translucent white wind streaks sweeping across, curling into a small aura spiral. It reads as A plus air plus sunlit sky, and the white-on-blue holds up small. Downscale the light 1024 PNG to 144x144 for the developer console and the appstore listing, checking legibility at that size. The yellow-to-blue echoes the same sky palette the hero already draws, so the icon and the app agree.

## Pebble reality checks

I know the platform is tiny; these are the guard rails.

- 64 colours. Every ramp is snapped to the 2-bit-per-channel Emery palette per `docs/PALETTE.md`. Tune against the SDK Color Picker on real hardware, because the grid shifts some hues.
- AppMessage budget. The digested snapshot is small and cheap. The two heavy additions are the bulletin text and any radar tile. Chunk the bulletin, send regional first, fetch national only when that card is opened, and treat radar as an experiment, not a promise.
- Memory. Draw one card at a time and free the rest; load and unload fonts around use; the bulletin string will be the biggest allocation, so cap its length on the phone.
- Battery. Keep the minute-tick refresh and the 30-minute fetch already in place; a card being open is just a redraw, not a new fetch.

## Roadmap

Near-term work continues under the current 1.0.0 tag, re-cut as it lands, per the present plan to keep retagging 1.0.0. Later phases become their own releases.

- Phase A, enrich the core (still 1.0.0). The hero edge fix and the snow scatter are done. Extend the data model and the pkjs bridge with wind, feels-like, UV, precipitation and moon, all from Open-Meteo worldwide. Add the wind, UV, sun-and-moon and details cards. Add the aviso card: the derived threshold advisory worldwide, upgraded to the official AEMET aviso in Spain. This alone brings Aura level with Touchy's day-to-day data, for everyone, not just Spain.
- Phase B, the Aura difference. The bulletin card: Aura's generated summary worldwide, the official AEMET regional and national boletin in Spain. The configurable card order and visibility. The reference-scale sheets and the touch gestures. Air quality on the MITECO path in Spain. After this, Aura carries features Touchy structurally cannot, and carries the words-and-warnings ones for the whole world.
- Phase C, the long tail. The nearest-station card (Spain), a radar tile if the budget allows, deeper astronomy (golden and blue hour), a news slot, and official warnings beyond Spain from the free MeteoAlarm (Europe) and weather.gov (United States) feeds. These match Touchy's remaining breadth and widen the official-warning tier.

## Why this beats Touchy without copying it

Touchy is the best generic weather app on Pebble: broad, configurable, playful. Aura reaches that breadth through Phases A to C and matches the configurability with user-ordered cards. Then Aura pulls ahead on things Touchy has no path to, because they are not features but an identity. For everyone, worldwide on free Open-Meteo, Aura turns the numbers into words and warnings, so no wearer gets a bare grid of figures the way Touchy leaves them. On top of that sit a design language shared with a real iOS and Apple Watch app, and the data-as-colour system where every value carries its own meaning in its own hue. And in Spain, where AEMET is home, Aura adds the official avisos, the regional and national boletines, the nearest station and the ICA air quality that no generic app can. Touchy tells you the weather. Aura shows you, in Aura's colours, in plain words wherever you are, with Spain's own forecast behind it when you are there.
