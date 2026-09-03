#pragma once
#include <pebble.h>

// The procedural day/night sky with the live sun (moon at night) arcing over it.
// Pure arithmetic and drawn shapes, no baked art. Ported from AuraSunPath /
// SolarTimes in the phone app; see docs/03-hero-and-sun-background.md.

typedef struct {
  int   cx, cy;   // disc centre, absolute screen coordinates
  int   radius;   // disc radius in px (shrinks toward the horizon)
  bool  night;    // true => draw a moon, false => the sun
  int   alt;      // altitude 0..100 (0 = horizon, 100 = zenith), for sky shading
} SunState;

// Compute the disc position/size for `now` from today's `sunrise`/`sunset`
// (unix seconds) inside the `sky` rectangle. Handles day, night (moon arc from
// sunset to the next sunrise) and missing/polar sun times (neutral high noon).
SunState sun_compute(time_t now, time_t sunrise, time_t sunset, GRect sky);

// Fill `sky` with the day/night gradient chosen by the sun's altitude and draw
// the disc (and, at night, a few static stars). Everything is bounds-relative.
void sky_draw(GContext *ctx, GRect sky, SunState s);
