# Aura palette on Pebble

Aura's visual identity isn't a logo or a fixed brand colour, it's a set of **data-driven colour ramps**: the value being shown (temperature, wind, air quality, UV) *is* the colour. Source of truth is `~/Projects/aura-apps/Sources/AuraKit/Palette.swift` (mirrored in `aura-android`'s `Palette.kt`), where colours are RGB floats in 0 to 1.

## The 64-colour constraint

The Pebble Time 2 (`emery`) renders **64 colours**: 2 bits per channel, so each of R/G/B can only be one of **`0x00 · 0x55 · 0xAA · 0xFF`** (0, 85, 170, 255). Every Aura colour has to be **snapped** to the nearest of those four levels per channel. Use the SDK [Color Picker](https://developer.repebble.com/guides/tools-and-resources/color-picker/) for the exact `GColor` constant name.

Snapping rule per channel: `level = round(value01 * 3) * 85`.

## Starter mapping (snapped anchors)

These are the ramp endpoints and anchors, snapped to the Pebble palette. Treat them as a **starting point** and tune against the Color Picker on real hardware, because the 64-colour grid shifts some hues (Aura's yellow, for instance, lands closer to orange).

### Temperature (cold to hot)
| Aura anchor | Source RGB (0 to 1) | 8-bit hex | Pebble-snapped |
|---|---|---|---|
| Cold violet | 0.40, 0.16, 0.56 | `#66298F` | `#5500AA` |
| Blue | 0.20, 0.52, 0.90 | `#3385E6` | `#55AAFF` |
| Green (≈20 °C boundary / accent) | n/a | n/a | `#00FF55` (`GColorMediumSpringGreen`) |
| Yellow | 0.97, 0.83, 0.26 | `#F7D442` | `#FFAA55` |
| Hot maroon | 0.40, 0.07, 0.13 | `#661221` | `#550000` |

The green boundary is the current **Aura accent** used in `aura-digital` (the battery rule).

### Wind (calm to violent)
| Aura anchor | Source RGB (0 to 1) | Pebble-snapped |
|---|---|---|
| Pale-blue calm | 0.55, 0.80, 0.92 | `#AAAAFF` |
| Violet (extreme) | 0.52, 0.28, 0.78 | `#AA55AA` |

### Air quality, MITECO ICA 1 to 6
| Band | Source RGB (0 to 1) | Pebble-snapped |
|---|---|---|
| 1 Good (blue) | 0.31, 0.66, 0.93 | `#55AAFF` |
| 2 Reasonable (green) | 0.30, 0.72, 0.42 | `#55AA55` |
| 3 Regular (yellow) | 0.96, 0.80, 0.25 | `#FFAA55` |
| 4 Unfavourable (red) | 0.90, 0.29, 0.24 | `#FF5555` |
| 5 Very unfav. (maroon) | 0.60, 0.13, 0.15 | `#AA0000` |
| 6 Extreme (violet) | 0.60, 0.28, 0.75 | `#AA55AA` |

### UV (WHO bands)
Green, then yellow, then **orange 0.97, 0.58, 0.18 mapping to `#FFAA55`**, then red, then violet (same endpoints as the temperature hot end).

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
