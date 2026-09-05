# 03 - Hero screen and the sun over the day/night background

## Problem

The hero is the screen the app opens on and the one thing that must feel like Aura. Its signature, and the specific must-have, is the live sun (moon at night) arcing over a sky that shifts from day to night, the effect that in the app only the two conditionless `wide_*_day` bases carry. On Pebble I get this almost for free because it is pure maths and drawn shapes, no baked art. This is the most portable idea in the whole project and it should ship first.

## Files

- `src/c/hero.c` (or a section of the main C file): the hero `Layer` update proc. New.
- `src/c/sky.c`: `sun_path()`, `solar_times()`, `sky_colors_at()`, the disc and star drawing. New, self-contained, no assets.
- Use `WeatherIcons-Regular.ttf` and `AtkinsonHyperlegibleNext-Bold.otf` from `resources/fonts/`.

## Layout (200 x 228 Emery, laid out from bounds so chalk and 144x168 still work)

Top two-thirds is the sky canvas with the moving disc. Bottom third is the readout, drawn over the darker foreground band so text stays legible.

- Sky canvas: `bounds` top, roughly `0 .. 0.62 * height`.
- Sun/moon disc: drawn at the computed `(x, y)` inside the sky canvas.
- Condition glyph: Weather Icons font, top-left of the readout band, chosen from `code` and night state.
- Current temp: large, centre, `"%d°"`. Degree only, unit implied by settings.
- Hi/lo: small, under the temp, `"%d° / %d°"` from `tmax`/`tmin`, tinted by the temperature ramp in `docs/PALETTE.md`.
- Location name: small, bottom, from `WX_NAME`.
- A one-line staleness note if `now - updated` is large.

## Sun-path maths (port of AuraSunPath)

All pure arithmetic, computed once per redraw from `now`, `sunrise`, `sunset` (the struct carries the last two as unix seconds). Re-date `sunrise`/`sunset` onto today if needed.

Day (`sunrise <= now <= sunset`):

```
f   = (now - sunrise) / (sunset - sunrise)   // 0 at sunrise, 1 at sunset
x   = f                                        // left/east to right/west
alt = sin(f * PI)                              // 0 -> 1 -> 0
y   = 0.80 - alt * 0.66                         // low at horizon, high at noon
```

Night (`now < sunrise` or `now > sunset`): fraction `g` from sunset to the next sunrise, `alt = sin(g * PI)`, `y = 0.60 - alt * 0.40` (a gentler moon arc than the sun's). Missing or polar sun times fall back to a neutral high-noon disc at `x = 0.5`.

Disc size shrinks toward the horizon: `radius = base * (0.62 + 0.38 * alt)`, so dawn and dusk sit low and small, noon sits high and full. Convert the normalised `(x, y)` to pixels against the sky canvas rect.

## Sky colours (port of Palette.skyBaseColors, snapped to 64)

The sky is a vertical gradient chosen by where the sun is, not by clock hour, so it tracks the real day. Pick two-to-three band colours by `alt` and day/night, each snapped to the Emery palette (`level = round(v * 3) * 85`):

- Deep night: indigo to near-black.
- Dawn / dusk (low sun, `alt` small): sunset orange low, violet-blue high (this is the Aura dawn/dusk accent already in `accent_for_hour`).
- Day (high sun): picton blue low, vivid cerulean high.

Draw the gradient as a few horizontal fills (Pebble has no true gradient primitive; 3 to 5 bands read fine at this size). At night, scatter a handful of static star pixels in the upper band; skip them by day. The disc is a filled circle with a one-pixel corona ring: warm (`GColorYellow`/`GColorOrange`) for the sun, pale grey/white for the moon.

## Refresh

The disc position changes slowly, so redraw the hero on `MINUTE_UNIT`, not on a sub-second timer. This is a watchapp the user is actively looking at, not a background face, so a minute tick is plenty and spares the battery. Recompute `sun_path()` inside the update proc from the current time each tick.

## Constraint

No baked backgrounds. The only thing that moves is the real sun or moon, drawn from real sun times, exactly the rule the app's hero art follows (`heroCarriesCondition`: the art never carries its own frozen sun). Sun times come from the phone so a remote location shows its own daylight, not the watch's.

## Edge cases

- (a) No forecast yet (fresh install, phone unreachable): still draw the sky and disc from the watch clock and a default location, so the signature shows before any data.
- (b) Polar or missing sun times: neutral high-noon disc, day sky, no crash.
- (c) `chalk` (round) and 144x168: everything is bounds-relative and the disc is clamped inside the sky rect, so it never clips off a round screen.
- (d) 1-bit `aplite`: the gradient collapses to black/white; rely on the disc outline and text contrast, per the palette doc.

## Done

- On Emery the sun rises low-left at dawn, arcs high at noon, sets low-right at dusk, and a moon takes over at night, all from real sun times.
- The sky colour tracks the sun, not the clock.
- The hero renders correctly with only the watch clock, before any phone data arrives.
