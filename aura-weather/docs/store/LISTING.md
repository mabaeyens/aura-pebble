# Aura Weather: appstore listing

Everything needed to submit Aura Weather as a new **watchapp** on the Pebble developer dashboard (developer.repebble.com/dashboard). This is a watchapp (`watchapp.watchface = false`), not a watchface, so the flow is "Add a Watchapp" and the dashboard asks for the app icons below, not a face screenshot alone.

## App identity (paste into the dashboard fields)

| Field | Value |
| --- | --- |
| Title | Aura Weather |
| UUID | `fce83b9e-37bc-4714-9125-840d83a72d64` |
| Version | 1.0.0 |
| Category | Tools & Utilities (it is a weather app; pick the closest available at submission time) |
| Source code | https://github.com/mabaeyens/aura-pebble |
| Support | https://askmira.es/aura/support (the same support page as the iOS and Android apps) |
| License | Code MIT; bundled fonts under SIL OFL 1.1 (see README) |

## Tagline

Worldwide weather with Aura's living sky, on your wrist.

## Description

Aura Weather carries my Spain weather app's design language onto the Pebble Time 2: a living sky you read at a glance. The hero screen draws the sun (a moon at night) arcing over a day-to-night gradient computed from the real sunrise and sunset times for your location, with no baked art, so it always matches the light outside.

Page through a deck of cards with UP and DOWN, with SELECT to refresh: the next eight hours, the next six days, a sun-and-moon card with golden and blue hour, a wind compass, UV and air-quality rings, a details grid, and a colour-coded warning card when one is active. Every number is tinted by Aura's own data ramps, so a hot day reads red, a calm wind reads teal, and the whole deck reads as one system. Cards appear only when they have something to say, and you choose which to show, reorder them, and pick the one the app opens on.

It fetches its own weather on the phone through Open-Meteo, worldwide, with no account and no API key, so it just works on install. In Spain you can switch to the official AEMET source with a free key. There is a light theme alongside the default dark one: long-press UP to switch it on the watch, or set it from the phone. The last forecast is kept, so a card shows the moment you raise your wrist, before the phone answers.

Free and open source.

## What is new in 1.0.0

First public release. The full card deck, worldwide Open-Meteo data with optional AEMET in Spain, the procedural sun-over-sky hero, and a light theme you can toggle on the watch or from settings.

## Icons

The dashboard asks for two app icons. Both are the shared Aura brand icon and already exist at the repo root; regenerate with `scripts/make-app-icons.sh` if the master changes.

| Asset | Size | File |
| --- | --- | --- |
| Small icon | 80x80 | `../../../docs/store/app-icon-80.png` |
| Large icon | 144x144 | `../../../docs/store/app-icon-144.png` |
| Pebble menu icon (bundled in the .pbw, launcher-tinted white on transparent) | 25x25 | `menu-icon.png` |

## Screenshots

Captured on the `emery` (Pebble Time 2) emulator at native 200x228. The dark deck is the primary set; the light set showcases the theme. Hero and aviso are theme-independent (the hero sits over its sky scene, the aviso keeps its warning colour), so they are not duplicated for light.

Dark theme (primary deck):

| Card | File |
| --- | --- |
| Hero | `weather-hero.png` |
| Aviso (warning) | `weather-aviso.png` |
| Hourly | `weather-hourly.png` |
| Daily | `weather-daily.png` |
| Sun and Moon | `weather-sun.png` |
| Wind | `weather-wind.png` |
| UV | `weather-uv.png` |
| Air quality | `weather-air.png` |
| Details | `weather-details.png` |

Light theme (showcase):

| Card | File |
| --- | --- |
| Hourly | `weather-hourly-light.png` |
| Daily | `weather-daily-light.png` |
| Sun and Moon | `weather-sun-light.png` |
| Wind | `weather-wind-light.png` |
| UV | `weather-uv-light.png` |
| Air quality | `weather-air-light.png` |
| Details | `weather-details-light.png` |

The `.pbw` targets `emery` (Pebble Time 2) only, so this one screenshot set covers the listing.

## Artifact

The single upload is the built bundle at `../../build/aura-weather.pbw` (run `pebble build` from `aura-weather/` to regenerate). It targets `emery` (Pebble Time 2) only.

## Submission steps

1. Sign in at developer.repebble.com/dashboard (GitHub sign-in).
2. Add a new **watchapp**, titled Aura Weather, with the UUID above (it must match the built `.pbw`).
3. Set the category, source-code URL and support contact from the identity table.
4. Upload the small (80x80) and large (144x144) icons.
5. Paste the tagline and description; add the "what is new" note as the release/changelog.
6. Upload the screenshots (dark deck first; the light set is optional colour).
7. Upload `aura-weather.pbw`.
8. Submit. There is no documented human review gate; the listing goes live once the assets and the `.pbw` are in.

The screenshots and this listing were prepared from the 1.0.0 build; regenerate the shots if the UI changes before you submit.
