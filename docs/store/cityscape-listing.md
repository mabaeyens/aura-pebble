# Aura Cityscape Weather: appstore listing

Everything needed to submit Aura Cityscape Weather as a new **watchapp** on the Pebble developer dashboard (developer.repebble.com/dashboard). It is a watchapp (`watchapp.watchface = false`), so the flow is "New" with the `.pbw`, then the fields and images below.

## App identity (paste into the dashboard fields)

| Field | Value |
| --- | --- |
| Title | Aura Cityscape Weather |
| UUID | `3b9d7e52-81c4-4f6a-a2d3-c5e90f1b7d62` (read from the .pbw; distinct from Aura Weather and Aura Sky) |
| Version | 1.0.0 |
| Category | Daily (the dashboard offers Daily, Tools & Utilities, Notifications, Remotes, Health & Fitness, Games; Aura Weather is filed under Tools & Utilities, so use that one if the three apps should sit together) |
| Website | https://askmira.es/aura/support |
| Source code | https://github.com/mabaeyens/aura-pebble |
| Support | https://askmira.es/aura/support (the same support page as the iOS and Android apps) |
| License | Code MIT; bundled fonts under SIL OFL 1.1 (see the README) |
| Visibility | Listed |

## Tagline

A small town under a living sky, on your wrist.

## Description

Aura Cityscape Weather turns the forecast into a small town. The sun or the moon rides its real arc over the skyline, and the sky and every layer of the scene change with the hour, from dawn through golden hour to a night of lit windows, computed from the real sunrise and sunset of your location. Clouds drift, rain falls (the scene only moves when you shake your wrist, to spare the battery), snow settles on the roofs and the pines, and a storm brings lightning, so you read the weather before you read a number.

The numbers sit on a plain panel under the scene: the temperature now, today's high and low, and the one extra fact that matters, the chance of rain when rain is likely and the wind speed otherwise. The thermometer tower in the scene fills to the current temperature in Aura's colour ramp, and the pennant on top of it streams out as the wind picks up.

Press DOWN for the next eight hours, a row of little windows where each tile is painted in that hour's sky, then again for the next six days as a skyline whose buildings grow with each day's high. SELECT asks the phone for a fresh forecast.

It fetches its own weather on the phone through Open-Meteo, worldwide, with no account and no API key, so it works on install. In Spain you can switch to the official AEMET forecast with a free key. Search a city, use the phone GPS, or type a latitude and longitude, and pick Celsius or Fahrenheit. The last forecast is stored, so the town is there the moment you open the app, and if the phone is out of reach it says how old the forecast is.

The town is one fixed skyline for every place. That is on purpose: the scene stays familiar, so what changes is the weather and the light.

Free and open source.

## What is new in 1.0.0

First release.

## Release notes (for the dashboard "Release Notes" field)

1. First release: a hero scene with the sun or moon on its real arc, five times of day and live weather effects (clouds, rain, snow, fog, lightning).
2. Hourly page: the next eight hours as little windows, each in that hour's sky.
3. Daily page: the next six days as a skyline, building height following each day's high.
4. Live data from Open-Meteo worldwide with no key, optional AEMET in Spain, city search, GPS or typed coordinates, Celsius or Fahrenheit.
5. Keeps the last forecast on the watch and says plainly when there is none yet or when it is getting old.

## Icons

The dashboard takes two app icons (both optional; it generates them from the name if missing). These are drawn for this app in Aura's blue gradient and warm corner glow, with a skyline that matches the scene. Regenerate with `aura-cityscape/tools/make-icons.py`.

| Asset | Size | File |
| --- | --- | --- |
| Small icon | 80x80 | `cityscape-app-icon-80.png` |
| Large icon | 144x144 | `cityscape-app-icon-144.png` |
| Pebble menu icon (bundled in the .pbw, launcher-tinted white on transparent) | 25x25 | `cityscape-menu-icon.png` (the same file as `aura-cityscape/resources/images/menu-icon.png`) |

## Banner

Optional. One per platform, shown at the top of the listing, exactly 720x320. Built from the real screenshots with `aura-cityscape/tools/make-banner.py`.

| Asset | Size | File |
| --- | --- | --- |
| Banner (emery) | 720x320 | `cityscape-banner.png` |

## Screenshots

Captured on the `emery` (Pebble Time 2) emulator at native 200x228 from the real build with the scripted scenarios compiled in and the scenario label hidden, so they show the final UI. The dashboard accepts at most five per platform, so upload these five in this order. The other three are extras for the README or a post.

| Order | Shot | File |
| --- | --- | --- |
| 1 | Clear day, hero | `cityscape-clear-day.png` |
| 2 | Rainy dusk, hero | `cityscape-rainy-dusk.png` |
| 3 | Clear night, hero | `cityscape-clear-night.png` |
| 4 | Hourly (clear day, the sky runs from day to night across the tiles) | `cityscape-hourly.png` |
| 5 | Daily (clear night) | `cityscape-daily.png` |
| extra | Golden hour, hero | `cityscape-golden-hour.png` |
| extra | Snow, hero | `cityscape-snow.png` |
| extra | Storm, hero | `cityscape-storm.png` |

The `.pbw` targets `emery` only, so this one set covers the listing. If you upload with `pebble publish` and local files, the names must start with the platform and an underscore (for example `emery_clear-day.png`), so copy them under those names first.

## Artifact

The single upload is `../../aura-cityscape/build/aura-cityscape.pbw`, the shipping build (run `pebble build` from `aura-cityscape/` with none of the `AURA_CITY_*` variables set). It targets `emery` only. The dashboard limit is 4.4 MB for the `.pbw`; check the file size before uploading.

## Submission steps

1. Sign in at developer.repebble.com/dashboard (GitHub sign-in) and click New.
2. Upload `aura-cityscape.pbw`. The dashboard reads the type, UUID, version and platform from it. Confirm the version is 1.0.0 and paste the release notes.
3. Enter the app name and the description, and choose the category.
4. Upload the 80x80 and 144x144 icons.
5. Upload the five emery screenshots, then the 720x320 banner.
6. Enter the website and source URLs from the identity table, keep visibility on Listed, and submit. There is no review queue; the listing goes live on submit.
7. Install it from the Pebble phone app onto a real Pebble Time 2 and check the forecast arrives, because GPS and a real watch were not part of the emulator tests.
