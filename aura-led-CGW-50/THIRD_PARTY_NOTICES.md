# Third-party notices

## PT2-LCD221 (digit rendering)

The routines in `src/c/lcd.h` that rasterise the slanted 7-segment digits (polygon fill, anti-aliased fill, dithered unlit segments and the digit layout) are adapted from **PT2-LCD221** by Kicou, <https://github.com/ChocolatineDigitale/PT2-LCD221>.

- Licence: MIT, Copyright (c) 2026 Kicou. Full text in `LICENSES/PT2-LCD221-MIT.txt`.
- Changes: the code was reduced to what this project needs and the theme settings were replaced by fixed colours.

## 7-Segment font (segment outlines)

The seven segment outlines in `src/c/segments.h` are taken from the **7-Segment** font by **Jan Bobrowski** ("~jb"), <https://torinak.com/font/7-segment>, through PT2-LCD221, which straightened the font's slight italic.

- Licence: SIL Open Font License 1.1. Full text in `LICENSES/OFL-1.1.txt`.
- The font file is not included in the built app, only these derived outlines. As a modified derivative of the font, `segments.h` stays under the same licence and is not sold on its own.
- The font carries no copyright line or Reserved Font Name.

## Fonts bundled in the app

- **Weather Icons** by Erik Flowers, SIL Open Font License 1.1, used for the weather glyph. Licence text: `fonts/WeatherIcons-LICENSE.txt`. Source: <https://github.com/erikflowers/weather-icons>.

## Design origin

The layout is inspired by the Casio CGW-50 "Cosmo Phase". This project is independent and non-commercial, and is not affiliated with or endorsed by Casio.

## DSEG (14-segment letters)

The date letters in `src/c/seg14.h` are polygons traced from DSEG14 Classic Bold by Keshikan, <https://github.com/ZanoZ/DSEG>, with `tools/gen_seg14.py`.

- Licence: SIL Open Font License 1.1. Full text in `LICENSES/DSEG-OFL-1.1.txt`.
- The font file is not included in the built app, only these derived outlines. As a modified derivative of the font, `seg14.h` stays under the same licence and is not sold on its own. The S and the 0 use the shapes of the 5 and the O instead of DSEG's own.

## Data

Weather, forecast and sun times are fetched from [Open-Meteo](https://open-meteo.com), CC BY 4.0. Weather data by Open-Meteo.com.
