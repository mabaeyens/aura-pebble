# Aura Weather (Pebble)

Specs for a standalone **weather watchapp** (not a watchface) for the Pebble Time 2, carrying Aura's design language. This folder holds the design work only. Nothing is built yet.

Aura on iOS (`../../aura-apps`) and Android (`../../aura-android`) is a Spain-only AEMET weather app. This app keeps AEMET as the Spanish source and adds worldwide coverage, which is net-new work rather than a straight port. No source code crosses over; only the design language and the sun-path maths do.

Read the specs in order:

- [`docs/00-portability-and-architecture.md`](docs/00-portability-and-architecture.md) is the starting point: what ports, what does not, the app's window flow, the on-watch data model, and the project scaffold. It also records the one product decision I need to settle (which worldwide provider).
- [`docs/01-data-bridge.md`](docs/01-data-bridge.md) is the phone-side JavaScript: AEMET for Spain, a worldwide provider for the rest, normalisation to one small code set, and the chunked AppMessage protocol.
- [`docs/02-location-search.md`](docs/02-location-search.md) is the "type any city, pick the closest match" settings flow, including how a Spanish result resolves to an AEMET municipality.
- [`docs/03-hero-and-sun-background.md`](docs/03-hero-and-sun-background.md) is the hero screen and its signature: the live sun (moon at night) arcing over a day/night sky, drawn procedurally with no baked art.
- [`docs/04-hours-and-days.md`](docs/04-hours-and-days.md) is the next-hours strip and next-days list.
- [`docs/05-companion-vs-standalone.md`](docs/05-companion-vs-standalone.md) answers whether Aura should be the companion app: what the Pebble side needs either way, and exactly what iOS and Android would each have to change if it is.

Built with Claude Code.
