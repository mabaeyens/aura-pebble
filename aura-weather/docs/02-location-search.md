# 02 - Location search (worldwide, with Spain resolving to AEMET)

## Problem

I want to type a city name anywhere in the world in settings and pick the closest matches, and when the pick is in Spain the app must still use AEMET, which is keyed by INE municipality code and knows nothing about lat/lon. The source app only ever searched the 8122-entry Spanish `municipios.json` with a substring filter; there is no worldwide geocoder to port. So this is two jobs: a worldwide typeahead, and a lat/lon-to-INE resolver for Spain.

The hard platform truth: a standard Clay config is declarative JSON and cannot do live typeahead against a remote geocoder. This needs a **custom Clay config page** (hand-written HTML/JS) with a search box, which is the most custom piece of the whole app.

## Files

- `src/pkjs/config.js`: Clay config. Reuse the Aura `clayStyle()` branding from `../aura-analog/src/pkjs/config.js`. Add one custom-HTML section that hosts the city search. New custom section.
- `src/pkjs/municipios.min.json`: a trimmed copy of `../../aura-apps/Sources/AuraKit/Resources/municipios.json` (842 KB, 8122 entries `{ine,nombre,provincia,latitude,longitude}`). Bundled on the phone side only. New file.
- `src/pkjs/geo.js`: `searchWorld(query)` typeahead and `nearestINE(lat, lon)`. New file.
- `src/pkjs/index.js`: on submit, stores the chosen location (name, lat, lon, ine-or-empty) and triggers a fetch.

## Worldwide typeahead

Use a keyless geocoder so search never needs a key even if the forecast provider does. Open-Meteo's geocoding endpoint fits and matches the forecast provider:

```
https://geocoding-api.open-meteo.com/v1/search?name=<query>&count=8&language=en&format=json
```

It returns `results[]` with `name`, `country`, `country_code`, `admin1` (region), `latitude`, `longitude`. Show up to 8 as "Name, Region, Country" rows; on tap, capture `{name, lat, lon, country_code}`. Debounce input ~300 ms. (If the worldwide-provider decision in `00` lands on OpenWeather, its `/geo/1.0/direct` returns the same fields and swaps in here; the rest of this spec is unchanged.)

This is the "returns the closest terms" behaviour: the geocoder already ranks by relevance and population, so the top rows are the closest matches to what was typed.

## Spain resolves to an AEMET municipality

AEMET has no lat/lon endpoint, so a Spanish pick must carry an INE code. Two ways to get there, and I do both:

1. If the geocoder result `country_code == "ES"`, run `nearestINE(lat, lon)` over the bundled `municipios.min.json`: minimum great-circle distance over the 8122 entries, returning the `ine`. This is the exact `MunicipioDatabase.nearest(lat,lon)` port and needs no key.
2. Offer a Spain-first search mode: when the query matches Spanish municipality names (accent- and case-folded substring, the `foldedForSearch` behaviour), show those rows directly from `municipios.min.json` with their INE already attached, so a Spanish user gets the official municipality name rather than the geocoder's rougher label.

Either way the stored location ends up as `{name, lat, lon, ine}` where `ine` is the 5-digit code for Spain and empty elsewhere. `index.js` and the provider router (`01`) key off whether `ine` is present.

## Where the 842 KB lives

On the phone, never on the watch. It ships inside the `.pbw` as a pkjs asset and is parsed in the JS sandbox, which has room the watch does not. Trim it to the four fields actually used (`ine,nombre,provincia,latitude,longitude`) and minify; drop any pretty-printing. If parse cost on low-end phones bites, lazy-load it only when the user opens the search box, and only build the search index on first keystroke.

## Manual and GPS fallbacks (keep from the faces)

The face bridge already supports manual lat/lon and GPS. Keep both as escape hatches under the search box:

- `LOCMODE` GPS: `navigator.geolocation.getCurrentPosition`; then `nearestINE` if the fix lands in Spain (bounding-box pre-check before the full scan to save work).
- Manual `LAT`/`LON` text inputs: for power users, same precedence the essential face documents (manual coords override the picked city).

## Constraint

Search must work with no forecast-provider key. A Spanish result must always carry an INE code, or it silently falls back to the keyless worldwide provider rather than showing nothing.

## Edge cases

- (a) Query matches nothing worldwide: show "No matches", keep the previous location.
- (b) A city near the Spanish border geocodes to `country_code != "ES"`: it uses the worldwide provider, which is correct (not an AEMET municipality).
- (c) GPS denied: fall back to the last picked city, then to a seed city (Madrid), never to a blank screen.
- (d) Same municipality name in two provinces (common in Spain): rows show `nombre, provincia` so the INE is unambiguous, matching the app.

## Done

- I can type "Tokyo", pick it, and see Japanese weather from the keyless provider.
- I can type "Teruel", pick it, and the stored location carries the right INE so the AEMET path (when enabled) queries the correct municipality.
- A GPS fix in Spain resolves to the nearest of the 8122 municipalities; a GPS fix abroad uses lat/lon directly.
