# Aura Weather (Pebble)

A standalone **weather watchapp** (not a watchface) for the Pebble Time 2, carrying Aura's design language. It fetches its own weather on the phone and needs no account and no companion app.

Aura on iOS (`../../aura-apps`) and Android (`../../aura-android`) is a Spain-only AEMET weather app. This app keeps AEMET as an optional Spanish source and adds worldwide coverage, which is net-new work rather than a straight port. No source code crosses over; only the design language and the sun-path maths do.

## What it does

A stack of cards, paged with UP and DOWN, with SELECT to force a refresh and BACK to exit. Holding SELECT on a card that has a scale (wind, UV, air quality) opens its reference sheet; any button closes it. Cards appear only when they have something to say (the aviso only during a warning, the UV ring only when the sun reaches a peak, the air card only where there is a reading), and you can hide any card and choose which one the app opens on from settings:

- **Hero**: the current temperature and condition over the signature Aura background, the live sun (a moon at night) arcing over a day/night sky drawn procedurally from real sun times, with no baked art. Hi/lo and the location name sit under it, and a warning pill when an aviso is active.
- **Aviso**: a colour-coded warning card in the four AEMET levels (verde, amarillo, naranja, rojo) with the phenomenon. Worldwide it shows a threshold advisory (storm, heavy rain, snow, wind, heat, cold, fog) derived from the numbers; in Spain an official AEMET aviso will override it. Shown only when a warning is active.
- **Hourly**: the next eight hours, each with the hour, a condition glyph (the night variant after sunset), a trend track spanning the day's min to max with a ramp-tinted dot at that hour's temperature, the temperature itself, and a thin bar when rain is likely.
- **Daily**: the next six days, each with the weekday, a condition glyph, and min/max tinted so a cold day reads blue and a hot day red.
- **Sun and Moon**: by day, a daylight arc with the sun riding sunrise to sunset and the times at the ends; after dark, the moon drawn at its real phase with the phase name and illumination.
- **Wind**: a needle over a compass rose, coloured by the wind ramp, with the speed, the direction it blows from, and the gust.
- **UV**: a ring from zero to today's peak in the WHO colour, the live index, and the band name (Low, Moderate, High, Very high, Extreme).
- **Air quality**: the 1-6 air-quality band as a ring in its ramp colour with the category name (Good to Extremely poor). Worldwide it comes from Open-Meteo's European AQI; the band lines up with Spain's ICA scale. Shown only where there is a reading.
- **Details**: feels-like, humidity, precipitation and gust, the fields the hero leaves off to stay calm.
- **Forecast**: Aura's own plain-language summary of the day, written from the numbers so no card is ever an empty grid; in Spain an official AEMET boletin will replace it.

The last forecast is persisted, so a card shows instantly on launch before the phone answers, and stays on screen if the phone is unreachable.

## Data

Weather comes from [Open-Meteo](https://open-meteo.com) by default, which needs no API key, so it stays free for anyone, worldwide. In Spain you can optionally switch to the official [AEMET](https://opendata.aemet.es) source by pasting a free AEMET key in settings; the app resolves your location to the nearest AEMET municipality (INE code) on the phone. Either way the watch sees one small normalised forecast and cannot tell which provider answered.

## Settings

Open the settings screen from the Pebble phone app (it is built with [Clay](https://github.com/pebble-dev/clay)):

- **Location**: type a city and pick from the worldwide search results, or turn on GPS, or type a latitude and longitude by hand.
- **Units**: automatic (following the phone locale), Celsius or Fahrenheit.
- **AEMET (Spain)**: optionally use AEMET instead of Open-Meteo in Spain, with your own free key.
- **Cards**: choose which card the app opens on, and toggle any card off to keep the stack to what you want. The hero is always shown and the aviso always appears during a warning.

## Build

```bash
export PATH="$HOME/.local/bin:$PATH"
npm install                                 # pebble-clay
pebble build                                # -> build/aura-weather.pbw (all platforms)
pebble install --emulator emery             # Emery = Pebble Time 2
```

The app targets `aplite`, `basalt`, `chalk`, `diorite` and `emery`. It is designed natively at 200x228 for the Time 2 and laid out from `layer_get_bounds()`, so the round `chalk` and the 144x168 legacy platforms still render.

## Design docs

The design work that led here, kept for reference:

- [`docs/00-portability-and-architecture.md`](docs/00-portability-and-architecture.md): what ports and what does not, the window flow, the on-watch data model, the project scaffold.
- [`docs/01-data-bridge.md`](docs/01-data-bridge.md): the phone-side JavaScript, provider routing, normalisation, the chunked AppMessage protocol.
- [`docs/02-location-search.md`](docs/02-location-search.md): the worldwide typeahead and how a Spanish result resolves to an AEMET municipality.
- [`docs/03-hero-and-sun-background.md`](docs/03-hero-and-sun-background.md): the hero screen and the sun over the day/night sky.
- [`docs/04-hours-and-days.md`](docs/04-hours-and-days.md): the next-hours strip and next-days list.
- [`docs/05-companion-vs-standalone.md`](docs/05-companion-vs-standalone.md): why v1 is standalone and what a companion path would cost.

## Fonts

Text is set in **Liberation Sans Bold** (metric-compatible with Helvetica), and conditions use the **Weather Icons** symbol font. Both are under the SIL Open Font License 1.1, bundled and subset to the glyphs actually used, under [`resources/fonts/`](resources/fonts) with their licenses.

- Liberation Sans, (c) Red Hat, SIL OFL 1.1.
- Weather Icons, (c) Erik Flowers, SIL OFL 1.1.

Built with Claude Code.
