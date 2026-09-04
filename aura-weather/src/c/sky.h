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
// `code` is the normalised condition (see weather.h): clear/few clouds draw the
// full vivid sky and disc, cloudy mutes the gradient toward grey but keeps the
// disc, and overcast-or-wetter flattens the sky to grey and hides the disc and
// stars, so the hero no longer shows a clear sun on a cloudy day.
void sky_draw(GContext *ctx, GRect sky, SunState s, uint8_t code);

// Draw the foreground landscape over the sky: a distant ridge, a snow-capped
// mountain range, green hills, and two trees. Each tree casts a ground shadow
// whose direction flips with the sun's horizontal position and whose length
// grows as the sun sinks (a port of AuraSky.drawScenery in the phone app).
// Drawn AFTER sky_draw so the mountains occlude a low sun. Greyed under
// overcast and silhouetted at night; shadows are cast only in daylight.
void scene_draw(GContext *ctx, GRect r, SunState s, uint8_t code);
