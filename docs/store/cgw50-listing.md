# Aura LCD: store listing

Built file: `aura-led-CGW-50/build/aura-led-CGW-50.pbw` (version 1.0.0, emery only).

## Title

Aura LCD

## Short description

A calm LCD watchface for the Pebble Time 2: sharp 7-segment time, a sun path with the countdown to sunset, weather and a 12 hour forecast.

## Description

A watchface that keeps the look of a classic Casio LCD and reorganises it for reading at a glance. The time is drawn in sharp slanted 7-segment digits and the date in 14-segment LCD letters. Under the date, a small LCD arch shows the sun's path with the time left until sunset (or sunrise at night). The bottom shows the current temperature with the day's low and high, and the next 12 hours of temperature and rain chance. After sunset the face turns negative with amber digits.

Three layouts (Quiet, Timeline and Stack), 13 colour themes with a day and a night look (including one that follows the weather), accent, date lettering, 24 hour or 12 hour time and units are all set in the settings page. Weather, forecast and sun times come from Open-Meteo, with a worldwide city search or the phone's GPS. No account or API key.

Inspired by the Casio CGW-50 "Cosmo Phase". An independent, non-commercial tribute, not affiliated with or endorsed by Casio. Free and open source.

Credits: digit rendering adapted from PT2-LCD221 by Kicou (MIT); segment shapes from the 7-Segment font by Jan Bobrowski and DSEG14 by Keshikan, and Weather Icons by Erik Flowers (all SIL OFL 1.1); weather data by Open-Meteo.com (CC BY 4.0).

## Release notes (1.2.0)

New: Stack can put its date and weather column on the left or the right of the time, and the column now uses the LCD lettering. The Timeline date uses the LCD lettering too. The unlit LCD segments are much fainter, so they no longer fight with the time. The forecast chart is taller and the small hour numbers under it are gone. Weather icons stay readable on every theme. Quiet, Timeline and Stack are re-spaced so the top and bottom margins match.

## Release notes (1.1.0)

13 colour themes, a sliced moon phase, the Panel layout removed, SUNSET IN and SUNRISE IN labels, credits on the settings page.

## Release notes (1.0.0)

First release.

## Screenshots (emery, 200 x 228)

- `cgw50-sun.png` (High contrast, direct sunlight)
- `cgw50-night.png` (Sand and amber, dark night)
- `cgw50-indoor.png` (Classic LCD, low light indoors)
- `cgw50-weather.png` (Mood: sunny, Timeline)
- `cgw50-stack.png` (Mood: sunset, Stack)
- `cgw50-*-clean.png`: the same five without the unlit LCD segments (not uploaded to the store)

## Menu icon

`cgw50-menu-icon.png` (25 x 25, white on transparent), also bundled as `resources/images/menu-icon.png`.

## Links

- Source: the public GitHub repo, folder `aura-led-CGW-50/`.
- Support email: to fill in at submission.

## Attributions required

PT2-LCD221 (MIT), the 7-Segment font and DSEG (SIL OFL 1.1), Weather Icons (SIL OFL 1.1), Open-Meteo (CC BY 4.0). Details in `aura-led-CGW-50/THIRD_PARTY_NOTICES.md`.
