# 05 - Companion app vs standalone (and what each costs Aura)

## The question

Do I make Aura (iOS and Android) the companion app that feeds the watch, or does the watchapp fetch its own data? This decides how much, if any, native app work the Pebble project drags in. The two are effectively mutually exclusive as the data transport: pick one.

## The two transports

### Path A - Standalone (PebbleKit JS)

The JavaScript inside the `.pbw` runs in the Pebble phone app's JS sandbox and fetches weather itself. This is what specs `01`-`04` assume.

- Aura iOS: **no change.**
- Aura Android: **no change.**
- Pebble side: everything already specced. `src/pkjs/*.js` does the fetching, geocoding and normalising; the C app just renders AppMessages.
- The Pebble app is fully self-contained. A user with no Aura app installed still gets the full experience.

Every one of the five must-haves (hero, hours, days, sun over the sky, worldwide search) is fully reachable this way. The only thing Path A cannot do is reuse Aura's own AEMET pipeline, so a Spanish user who wants the official AEMET source must paste a free AEMET key in settings (the decision in `00`).

### Path B - Companion (PebbleKit iOS / Android)

The native Aura app talks to the watch directly over the PebbleKit SDK and pushes it the `WeatherSnapshot` it already computes. The watch gets AEMET data using Aura's own key and pipeline, with no key step for the user.

What this needs, and it is not small:

- Pebble side (small):
  - A `companions` block in the app manifest declaring Aura as the companion (iOS App Store URL, Android package name, `pebblekit_version`). This makes launching the watchapp wake Aura on the phone. Verify the exact manifest keys against the installed SDK, since this is legacy-carried wording.
  - The C watchapp is otherwise unchanged: same windows, same AppMessage inbox keys from `01`. Only the sender changes. The pkjs bridge is dropped, or kept as a fallback for users without Aura.
- Aura iOS (real work): link PebbleKit iOS, open a session to the app UUID (`PBPebbleCentral` -> `PBWatch`), serialise `WeatherSnapshot` into the same integer message-key dictionary `01` defines, push on refresh and on a watch request, and handle backgrounding. iOS only grants limited background execution, so keeping the watch fed while Aura is backgrounded is the hard part and the main risk.
- Aura Android (separate real work): link PebbleKit Android, run a background `Service`, send the same dictionary. Android backgrounding is more permissive than iOS, so this side is easier, but it is still a separate SDK and codebase from iOS.

So the answer to "do I modify iOS or Android": for Path B, **both, and separately.** They share nothing; PebbleKit iOS is Swift/ObjC and PebbleKit Android is Java/Kotlin, and each needs its own integration, its own backgrounding handling, and its own app-store release.

## What Path B actually buys

Only one thing that Path A cannot already do: Spanish users get AEMET (and, later, Aura's observations, CAP warnings and bulletins) **without pasting an AEMET key**, because Aura holds the key and the pipeline. Worldwide search, the sun maths, the hero, hours and days all work identically on Path A with no native app at all. So the entire value of the companion path is removing one settings step for Spanish users who already run Aura, plus a future door to the richer AEMET extras I deferred in `00`.

## Recommendation

Ship **Path A (standalone JS)** for v1. It gets all five must-haves, needs zero changes to iOS or Android, and works for users who do not have Aura installed. It is the fastest route to something on the appstore.

Treat **Path B as an optional Phase 2**, and only for the AEMET-without-a-key win. If I do it, I would do Android first (easier backgrounding) to prove the snapshot-forwarding, then iOS. Before committing, verify that PebbleKit iOS/Android is actually available and maintained for the 2025 Core Devices relaunch, not just the 2016 platform, because the SDK prose in this repo is legacy-carried and this is exactly the kind of claim to check against reality first.

A clean hybrid is also on the table and worth noting: keep Path A as the universal default, and if Path B ships later, have the watchapp prefer the companion when Aura answers and fall back to the JS bridge when it does not. That keeps non-Aura users working and gives Aura users the key-free AEMET path, at the cost of maintaining both senders.

## Bottom line for you

- Want it live soonest with no app changes: Path A. Nothing to touch in iOS or Android.
- Want Spanish Aura users to skip the AEMET key and, later, get Aura's warnings and observations on the wrist: Path B, which means PebbleKit work in both native apps and a release of each.
- My call: A now, B later and only if the key step proves to be real friction for Spanish users.
