# Aura palette on Pebble

Aura's visual identity isn't a logo or a fixed brand colour, it's a set of **data-driven colour ramps**: the value being shown (temperature, wind, air quality, UV) *is* the colour. Source of truth is `~/Projects/aura-apps/Sources/AuraKit/Palette.swift` (mirrored in `aura-android`'s `Palette.kt`), where colours are RGB floats in 0 to 1.

## The 64-colour constraint

The Pebble Time 2 (`emery`) renders **64 colours**: 2 bits per channel, so each of R/G/B can only be one of **`0x00 · 0x55 · 0xAA · 0xFF`** (0, 85, 170, 255). Every Aura colour has to be **snapped** to the nearest of those four levels per channel. Use the SDK [Color Picker](https://developer.repebble.com/guides/tools-and-resources/color-picker/) for the exact `GColor` constant name.

Snapping rule per channel: `level = round(value01 * 3) * 85`.

## The weather ramps as encoded in C

These four ramps are re-encoded from `Palette.swift` into the nearest Pebble 64-colour constant in `aura-weather/src/c/aura-weather.c`, and I keep them in step with the phone so a colour means the same thing on the watch as it does in the iOS/macOS app. Each row is one band: the threshold, the iOS intent behind the colour, and the `GColor` constant I snap it to.

### Temperature (`temp_color`, degrees C)

Mirrors `Palette.swift` `tempStops`, including the green/yellow hand-off at 20 C.

| Band (°C) | iOS intent | Pebble GColor |
|---|---|---|
| < -4 | deep cold violet | `GColorIndigo` |
| < 2 | blue-violet | `GColorLiberty` |
| < 8 | blue | `GColorBlueMoon` |
| < 13 | cyan-teal | `GColorTiffanyBlue` |
| < 17 | green-teal | `GColorMalachite` |
| < 20 | mild accent green | `GColorGreen` |
| < 23 | green-yellow | `GColorInchworm` |
| < 26 | yellow | `GColorYellow` |
| < 29 | amber | `GColorChromeYellow` |
| < 32 | orange | `GColorOrange` |
| < 35 | red-orange | `GColorSunsetOrange` |
| < 39 | red | `GColorRed` |
| < 43 | dark red | `GColorDarkCandyAppleRed` |
| ≥ 43 | deep maroon | `GColorBulgarianRose` |

### Wind (`wind_color`, km/h)

From `Palette.swift` `windStops` (the Windy ramp); the band edges track the Beaufort forces.

| Band (km/h) | iOS intent | Pebble GColor |
|---|---|---|
| < 12 | calm | `GColorCeleste` |
| < 20 | light | `GColorMediumAquamarine` |
| < 29 | moderate | `GColorGreen` |
| < 39 | fresh | `GColorYellow` |
| < 50 | strong | `GColorOrange` |
| < 62 | very strong | `GColorSunsetOrange` |
| < 89 | gale | `GColorRed` |
| < 118 | storm | `GColorPurpureus` |
| ≥ 118 | violent | `GColorVividViolet` |

### UV index (`uv_color`, WHO bands)

From `Palette.swift` `UVBands`.

| Index | Band | Pebble GColor |
|---|---|---|
| 0–2 | Low | `GColorGreen` |
| 3–5 | Moderate | `GColorYellow` |
| 6–7 | High | `GColorOrange` |
| 8–10 | Very high | `GColorRed` |
| 11+ | Extreme | `GColorVividViolet` |

### Air quality (`aqi_color`, MITECO ICA 1 to 6)

From `Palette.swift` `airQuality`, the Spanish MITECO ICA / European index. **This ramp is inverted** from the usual WHO-style green-at-best convention: MITECO reads blue as best and runs down to violet as worst, and I match the phone rather than the common scale, so a low index reads azul, not verde.

| Index | Band (ES) | Pebble GColor |
|---|---|---|
| 1 | Good (azul) | `GColorVividCerulean` |
| 2 | Fair (verde) | `GColorGreen` |
| 3 | Moderate (amarillo) | `GColorYellow` |
| 4 | Poor (rojo) | `GColorRed` |
| 5 | Very poor (granate) | `GColorDarkCandyAppleRed` |
| 6 | Extremely poor (violeta) | `GColorVividViolet` |

## Time-of-day accent (the `aura-digital` face)

The digital face has no weather data, so its accent tracks the **time of day** instead, echoing Aura's sun-tracking sky on the phone. `accent_for_hour(int hour)` in `aura-digital/src/c/aura-digital.c` maps the hour to a palette colour:

| Hours | Phase | GColor | Hex |
|---|---|---|---|
| 00–04 | Deep night | `GColorIndigo` | `#5500AA` |
| 05–06 | Dawn | `GColorSunsetOrange` | `#FF5555` |
| 07–09 | Morning | `GColorPictonBlue` | `#55AAFF` |
| 10–14 | Midday | `GColorVividCerulean` | `#00AAFF` |
| 15–17 | Afternoon | `GColorRajah` | `#FFAA55` |
| 18–20 | Dusk | `GColorSunsetOrange` | `#FF5555` |
| 21–23 | Night | `GColorIndigo` | `#5500AA` |

This is the palette's stand-in for the phone's continuous sky gradient: a stepped ramp, since per-minute recomputation on a watchface doesn't warrant interpolation.

## Notes

- Keep these values **copied per face project**, not abstracted into a shared library, since the faces are independent Pebble projects by design (see `CLAUDE.md`).
- On the 1-bit legacy platform (`aplite`) these all collapse to black and white; rely on layout and contrast there, not colour.
