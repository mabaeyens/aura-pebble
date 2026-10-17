# Aura LCD: descriptions and marketing copy

Tone follows the Aura Essential listing and README: plain, first person, one glanceable idea, free and open source, emery only. No em-dashes.

## Names and taglines

- Store name: Aura LCD
- Tagline (under 80 characters): An 80s LCD watch, rebuilt sharp for the Pebble Time 2.
- Alternates: Three watchfaces in one, all LCD. / A calm LCD face with the sun on it. / Slanted segments, a sun path and the weather, nothing else.

## Store description (long)

Aura LCD is the LCD watch I wanted as a kid, rebuilt for the Pebble Time 2. The time is drawn in sharp, slanted 7-segment digits, the date in 14-segment letters, all rendered pixel by pixel so the edges stay crisp instead of the jagged look you get from a font.

Above the time, a small LCD arch shows the path of the sun. Only the current column lights up, so one glance tells you how much daylight is left, next to a countdown to sunset (or to sunrise at night). Below it, the temperature with the day's low and high, and the next 12 hours of temperature and rain chance.

After dark the face turns negative with amber digits, so it stays easy on the eyes and quiet on the wrist.

Three watchfaces in one! The styles in the settings are three different layouts, from the calm default to a dense one: Quiet, Timeline and Stack. Thirteen colour themes, each with a day and a night look, including one that follows the weather, plus accent colour, date lettering, 12 or 24 hour time and units are all in the settings. Weather, forecast and sun times come from Open-Meteo, with a worldwide city search or your phone's location. No account, no API key, nothing to sign up for.

Free and open source. Inspired by the Casio CGW-50 Cosmo Phase; an independent, non-commercial tribute, not affiliated with or endorsed by Casio.

Digit rendering is adapted from PT2-LCD221 by Kicou (MIT). Segment shapes come from the 7-Segment font by Jan Bobrowski and DSEG14 by Keshikan, and weather icons from Weather Icons by Erik Flowers (all SIL OFL 1.1). Weather data by Open-Meteo.com (CC BY 4.0).

## Store description (short, for the 1-2 line slot)

Sharp slanted LCD digits, a sun-path arch with a sunset countdown, weather and a 12 hour forecast. Three watchfaces in one and thirteen themes, free and open source.

## Release notes (1.1)

New: 13 colour themes with a day and a night look (Classic LCD, High contrast, Black on yellow, Paper and ink, Sand and amber, Sky, six weather moods, and one that follows the current weather). New Casio-style sliced moon phase. Label "SUNSET IN" / "SUNRISE IN" on the countdown. Credits on the settings page. The Panel layout is gone; Quiet, Timeline and Stack remain.

## Release notes (1.0)

First release. Three layouts (Quiet, Timeline, Stack), 13 colour themes (one follows the weather), sharp 7-segment time and 14-segment date, sun-path arch with sunset and sunrise countdown, current weather with a 12 hour forecast, day and night themes, and a settings page with worldwide city search.

## Feature bullets

- Sharp slanted 7-segment time, drawn pixel by pixel
- 14-segment LCD date lettering
- Sun-path arch with a countdown to sunset or sunrise
- Temperature, daily low and high, 12 hour forecast with rain chance
- Three watchfaces in one: Quiet, Timeline and Stack layouts
- 13 colour themes with a day and a night look, one that follows the weather
- Negative amber night mode
- Open-Meteo weather, no account or API key
- Free and open source

## Social posts

### Short (Mastodon, Bluesky, the Pebble Discord)

New Pebble Time 2 face: Aura LCD. Sharp slanted 7-segment time, 14-segment date, and a little LCD sun arch counting down to sunset. Three layouts, thirteen themes, free and open source. Inspired by the Casio Cosmo Phase.

https://apps.repebble.com/75df0aa5863e415eaa84c66b

### Medium (LinkedIn or a blog post)

I grew up wanting one of those 80s LCD watches, the kind with a planet diagram on top and chunky segments underneath. I never got one. Aura LCD is my take on it for the Pebble Time 2.

The hard part was making it look sharp. Pebble's font renderer turns thin, slanted segment fonts into jagged edges, so I drew every digit and letter as polygons with my own anti-aliasing. The time is 7-segment, the date is 14-segment, and the result reads cleanly at arm's length.

The rest is deliberately calm: a small arch showing where the sun is, a countdown to sunset, the temperature, and the next 12 hours of rain. At night the whole face goes negative with amber digits.

It has three layouts and thirteen colour themes, free and open source, no account needed, weather from Open-Meteo. Inspired by the Casio CGW-50 Cosmo Phase, and an independent tribute, not affiliated with Casio.

App store: https://apps.repebble.com/75df0aa5863e415eaa84c66b
Source: https://github.com/mabaeyens/aura-pebble/tree/main/aura-led-CGW-50

### Spanish (for askmira.es and Spanish-speaking channels)

Nueva esfera para el Pebble Time 2: Aura LCD. Dígitos de 7 segmentos inclinados y nítidos, fecha en 14 segmentos, y un pequeño arco LCD con el recorrido del sol y la cuenta atrás hasta la puesta. Cuatro diseños, modo nocturno en ámbar, gratis y de código abierto. Inspirada en el Casio Cosmo Phase, sin relación con Casio.

## Hooks to pick from

- "The 80s LCD watch I never had, rebuilt for the Pebble Time 2."
- "Pebble fonts make thin LCD segments look jagged, so I drew every digit as a polygon."
- "One glance: how much daylight is left."
- "Calm on purpose. Three layouts, no clutter."

## Notes

- The Casio name only appears as "inspired by" with the non-affiliation line. Keep that wherever the face is described.
- The listing is hidden until it is made visible on the dashboard. Post nothing with the link before then.
- Screenshots for posts: `cgw50-sun.png` first, `cgw50-night.png` second; the other three as a follow-up.
