# 04 - Next hours and next days

## Problem

After the hero, the two forecast screens: the next hours and the next days. In the app these are `WeatherSnapshot.hours` and `WeatherSnapshot.days`, rich rows behind frosted cards. On the watch they become two compact, glanceable lists fed by the already-digested `HourSlot`/`DaySlot` arrays the phone sends (`01-data-bridge.md`). No parsing, no scrolling weather logic on the watch; just draw the arrays.

## Files

- `src/c/forecast.c`: the hourly and daily `Layer` update procs, or two sections of the main file. New.
- Reuse the Weather Icons font and the temperature ramp helper from the hero.

## Hourly screen

`hours[8]`, each `{temp, code, pop}`, anchored to the current hour. The phone re-anchors to the real current hour before sending, the way the app's `resolved(at:)` does, so slot 0 is always "next hour".

Layout, one row per hour, 8 rows down the 228px height (roughly 26px each):

- Left: the hour label. Derive the clock time on the watch by adding the slot index to the current hour (`(now_hour + idx) % 24`), formatted per the user's 12/24h system setting.
- Middle: the condition glyph (Weather Icons font, small), night variant chosen from whether that hour falls after `sunset` / before `sunrise`.
- Right: temp `"%d°"`, tinted by the temperature ramp.
- A thin precip bar or `pop%` when `pop` is non-trivial (say >= 20), so rain reads at a glance. Keep it a single drawn bar, not a chart.

## Daily screen

`days[6]`, each `{min, max, code, pop}`, slot 0 = today. Layout, one row per day:

- Left: weekday, three letters, derived on the watch from today plus the slot index. Reuse the bundled letterform approach the essential face uses for its weekday so it matches Aura's type.
- Middle: the condition glyph.
- Right: `min` and `max`, `"%d° %d°"`, each tinted by the ramp so a cold day reads blue and a hot day reads red without labels.
- Optional: a small precip marker when `pop` is high.

## Navigation

`UP`/`DOWN` page Hero -> Hourly -> Daily and back (the screen index in `00`). No per-row scrolling in v1; eight hours and six days each fit one screen at Emery height. If a future platform is shorter, switch that screen to a `MenuLayer`, but avoid the extra weight until needed.

## Rendering

Both screens are single custom-drawn `Layer` update procs reading the persisted `Weather` struct, redrawn when a new forecast lands (the inbox handler marks the visible screen dirty) and when the user pages to them. No animation, no tick subscription on these two screens.

## Constraint

Draw only from the compact arrays already in memory. The watch must never compute a forecast value, only format and tint what the phone sent. Tinting always goes through the single `docs/PALETTE.md` temperature ramp so the colour language matches the hero and the app.

## Edge cases

- (a) Fewer slots than expected (provider returned a short forecast): draw the slots present, leave the rest blank, never read past the array.
- (b) Negative temps: the format handles the sign; the ramp clamps at its cold anchor.
- (c) Night hours on the hourly screen: pick the night glyph from sun times, matching the hero, so a clear night hour shows a moon not a sun.
- (d) `chalk` round screen: rows are bounds-relative and centred; the outermost rows may lose a pixel of margin, which is acceptable, or drop to 6 hourly rows on round.

## Done

- Paging down from the hero shows eight upcoming hours with glyph, temp and rain, then six days with weekday, glyph and hi/lo.
- Every temperature on both screens is tinted by the shared ramp.
- Both screens update the instant a new forecast arrives and render from persist on launch.
