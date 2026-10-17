# Aura LCD

A Pebble Time 2 watchface that keeps the Casio CGW-50 "Cosmo Phase" LCD look and reorganises it for reading at a glance. The time is drawn in sharp slanted 7-segment digits, the date in 14-segment LCD letters, and the rest of the face is quiet: a sun path with the countdown to the next sunset or sunrise, the current weather with the day's low and high, and a 12 hour forecast of temperature and rain chance. After sunset the face turns negative with amber digits. It is display only, because watchfaces do not receive touch events on the current firmware.

## Layouts

Three layouts share the same design and are picked in the settings page. Quiet is the default: date and moon on top, the sun path with its countdown, a large time, then the weather on the left and the forecast on the right. Timeline moves the weather up next to the date and stacks the sun path over a full-width forecast. Stack draws the hours over the minutes, very large, with a narrow column of date and weather beside them, on the right or the left as you choose.

## Settings

The settings page (Clay) sets the layout, the theme (automatic, always day, always night), the background and digit colours for day and night, the accent colour (or the automatic sky colour by hour), the date lettering (14-segment LCD, dot matrix, bold dots or Gothic), 24 hour or 12 hour time, day-month or month-day, the unlit LCD segments, units, and the weather location (a worldwide city search, manual coordinates or the phone's GPS). If a colour choice leaves too little contrast, the digits fall back to black or white so the time always stays readable.

## Data

Weather, forecast and sun times come from [Open-Meteo](https://open-meteo.com), with no API key. Weather data by Open-Meteo.com, licensed CC BY 4.0.

## Design origin

The layout and idea come from the Casio CGW-50 "Cosmo Phase" watch. This is an independent, non-commercial reimplementation made as a tribute. It is not affiliated with or endorsed by Casio, and all code here is original except where credited below. Casio and Cosmo Phase are trademarks of their owners.

## Credits

- **PT2-LCD221** by Kicou, <https://github.com/ChocolatineDigitale/PT2-LCD221>, MIT licence (`LICENSES/PT2-LCD221-MIT.txt`). The way the digits are drawn, rasterised straight into the framebuffer as slanted polygons with anti-aliased edges and dithered unlit segments, is ported from that project into `src/c/lcd.h`. It is what makes the numbers sharp on the Pebble Time 2 screen.
- **7-Segment** font by Jan Bobrowski, <https://torinak.com/font/7-segment>, SIL Open Font License 1.1 (`LICENSES/OFL-1.1.txt`). The segment outlines in `src/c/segments.h` come from this font through PT2-LCD221, and stay under the same licence. The font file itself is not part of the built app.
- **DSEG** by Keshikan, <https://github.com/ZanoZ/DSEG>, SIL Open Font License 1.1 (`LICENSES/DSEG-OFL-1.1.txt`). The 14-segment letters in `src/c/seg14.h` are polygons traced from DSEG14 Classic Bold with `tools/gen_seg14.py`. The font file is kept in `fonts/` only to regenerate them and is not part of the built app.
- **Weather Icons** by Erik Flowers, SIL Open Font License 1.1, used for the weather glyph. Licence text: `fonts/WeatherIcons-LICENSE.txt`. Source: <https://github.com/erikflowers/weather-icons>.

Full details are in `THIRD_PARTY_NOTICES.md`.

## Building

```sh
pebble build
pebble install --emulator emery
```

Changing the message keys needs a `pebble clean` first.

## Built with

[Claude Code](https://claude.com/claude-code) (Anthropic).
