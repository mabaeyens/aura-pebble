# Backlog

## Done

- 2026-09-02 Essential polish pass: sun rays/outline uniform 3px; overcast (WMO 3) now a plain cloud with no sun; clouds redrawn with a scalloped, rounded bottom (row of bump discs) instead of a flat bar; weekday moved from Gothic to the bundled **LECO 1976 Regular** so it matches the LECO numbers; battery resized to sit level with the other icons and to read its fill clearly; the top-block icon + label group vertically centred (measured 25px above / 25px below).
- 2026-09-02 Regenerated all five store screenshots on the polished face, each calendar showing a distinct day + weekday (classic 9 Fri, ocean 18 Sun, midnight 11 Wed, forest 23 Mon, sunset 30 Thu) and a varied complication mix.
- 2026-09-02 Credited LECO 1976 Regular (© Samuel Čarnoký / CarnokyType) and its free MyFonts Desktop + App license in the README Fonts section; confirmed the font is free for both the embedded `.pbw` and the store screenshots.
- 2026-09-02 Released aura-essential 1.0.0 on GitHub (tag `essential-v1.0.0`): clean production `.pbw`, a fresh source zip, and the five store screenshots as assets.
- 2026-09-02 README hero row now features the Essential face in three themes (classic, ocean, midnight), replacing the analog Mondaine preview images.
- 2026-09-02 Partly-cloudy weather icon: sun now drawn on top of the cloud and fully visible, with a uniform 3px black outline all the way around (same disc as the outer halo, skipped in the black halo passes so it does not compound).
- 2026-09-02 Calendar date vertically centred in the white body by measuring the LECO glyph (`graphics_text_layout_get_content_size`, 5-arg form) and placing it at `cy+3`, instead of top-anchoring the box which bottom-aligned the digits.
- 2026-09-02 Added five store screenshots under `docs/store/` (classic, ocean, forest, midnight, sunset), each a different block-colour theme and complication mix.
- 2026-09-01 aura-essential built: three-block Essential replica with three configurable complications, LECO time and numeric labels, bitmap shoe icon, Clay settings, and the Open-Meteo weather bridge.

## Pending

- Publish aura-essential to the Pebble appstore (apps.repebble.com). Needs a browser `pebble login` (Firebase OAuth) that I cannot complete headlessly, then `pebble publish`. A dry-run check of what `publish` would send is offered once logged in.
- Decide what to do with `docs/analog-light.png`, `docs/analog-dark.png` and `docs/emery-preview.png`: no longer referenced by the README after the hero change, but the analog and digital faces still exist. Keep, reuse, or remove.

## Notes

- Store screenshots are now generated **without patching the source**: build+install once, then drive each theme with `pebble send-app-message --emulator emery --app-uuid <uuid> --int <key>=<val> ...` using the runtime message-key IDs (SLOT1..3 = 10000..10002, TOPCOLOR/BANDCOLOR/BOTCOLOR = 10003..10005, SEPCOLOR 10007, COMPCOLOR 10008, TIMECOLOR 10009, WX_TEMP/WX_CODE/WX_OK = 10015..10017). Inject demo data with `emu-steps 8432`, `emu-battery --percent 100`, `emu-heart-rate 68`, and set the calendar day via `emu-set-time <unix>` (compute the epoch against Europe/Madrid so the clock reads 10:09 despite CET/CEST). Heart-rate gotcha: set the HR **before** the redraw-triggering `emu-set-time` (with a short sleep) or the bpm label renders empty. This leaves the tracked source completely untouched — no restore step.
- After any store-shot run, `aura-essential/build/aura-essential.pbw` still holds the last demo *settings* in emulator persist, but the tracked source is clean; rebuild with `pebble build` for a shippable `.pbw`.
- The emery emulator mutes saturated hues in its display simulation (canonical orange renders as coral). A real Time 2 shows it closer to the true `GColorOrange`. Not a bug.
- Publishing path: `pebble publish` uploads to the appstore dashboard API at `https://appstore-api.repebble.com` (override with `--api-base` or `PEBBLE_APPSTORE_API_BASE`), authenticated with a Firebase ID token from `pebble login`. `pebble install --cloudpebble`/`--phone` use the CloudPebble relay to sideload to a real watch. Login status was logged out at the end of this session.
