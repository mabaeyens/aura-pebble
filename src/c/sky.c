#include "sky.h"

#define SECS_PER_DAY 86400
#define BASE_SUN  14   // noon sun radius (px, before the horizon shrink)
#define BASE_MOON 11   // moon a touch smaller than the sun

// sin(fraction * PI) for fraction in [0,1], returned as 0..1000 (fixed point).
// Uses the SDK trig table so we need no libm. The arc peaks (1000) at fraction
// 0.5 and is 0 at the ends, exactly the sun's altitude curve over the day.
static int32_t sin_pi_1000(int32_t frac_1000) {
  if (frac_1000 < 0) frac_1000 = 0;
  if (frac_1000 > 1000) frac_1000 = 1000;
  // fraction*PI spans half of TRIG_MAX_ANGLE (a full 2*PI turn).
  int32_t angle = (TRIG_MAX_ANGLE / 2) * frac_1000 / 1000;
  int32_t s = sin_lookup(angle);              // -TRIG_MAX_RATIO..TRIG_MAX_RATIO
  if (s < 0) s = 0;                            // clamp tiny negative table noise
  return s * 1000 / TRIG_MAX_RATIO;            // 0..1000
}

// Re-date a sunrise/sunset that may belong to a different calendar day onto the
// day of `now`, so the arc is correct even if the persisted times are stale.
static time_t redate(time_t t, time_t now) {
  if (t == 0) return 0;
  while (t - now >  SECS_PER_DAY / 2) t -= SECS_PER_DAY;
  while (now - t >  SECS_PER_DAY / 2) t += SECS_PER_DAY;
  return t;
}

SunState sun_compute(time_t now, time_t sunrise, time_t sunset, GRect sky) {
  SunState st = { .cx = 0, .cy = 0, .radius = BASE_SUN, .night = false, .alt = 100 };

  int32_t fx_1000 = 500, alt_1000 = 1000;   // normalised x and altitude, 0..1000
  int32_t y_1000  = 250;                     // normalised y (0 top, 1000 bottom)
  bool night = false;

  sunrise = redate(sunrise, now);
  sunset  = redate(sunset, now);

  if (sunrise == 0 || sunset == 0 || sunset <= sunrise) {
    // Missing or polar sun times: a neutral high-noon disc, day sky, no crash.
    fx_1000 = 500; alt_1000 = 1000;
    y_1000  = 800 - alt_1000 * 660 / 1000;   // y = 0.80 - alt*0.66
    night = false;
  } else if (now >= sunrise && now <= sunset) {
    // Daytime: sun sweeps left(east) to right(west), high at noon.
    fx_1000  = (int32_t)(now - sunrise) * 1000 / (sunset - sunrise);
    alt_1000 = sin_pi_1000(fx_1000);
    y_1000   = 800 - alt_1000 * 660 / 1000;  // y = 0.80 - alt*0.66
    night = false;
  } else {
    // Night: a gentler moon arc from sunset to the next sunrise.
    night = true;
    time_t prev_set, next_rise;
    if (now < sunrise) { prev_set = sunset - SECS_PER_DAY; next_rise = sunrise; }
    else               { prev_set = sunset;                next_rise = sunrise + SECS_PER_DAY; }
    int32_t span = (int32_t)(next_rise - prev_set);
    int32_t g_1000 = span > 0 ? (int32_t)(now - prev_set) * 1000 / span : 500;
    fx_1000  = g_1000;
    alt_1000 = sin_pi_1000(g_1000);
    y_1000   = 600 - alt_1000 * 400 / 1000;  // y = 0.60 - alt*0.40 (gentler)
  }

  // radius = base * (0.62 + 0.38*alt): dawn/dusk sit low and small, noon full.
  int base = night ? BASE_MOON : BASE_SUN;
  st.radius = base * (620 + 380 * alt_1000 / 1000) / 1000;
  st.night  = night;
  st.alt    = alt_1000 / 10;                 // 0..100

  // Normalised (fx, y) -> pixels inside the sky rect.
  st.cx = sky.origin.x + (int)((int32_t)sky.size.w * fx_1000 / 1000);
  st.cy = sky.origin.y + (int)((int32_t)sky.size.h * y_1000  / 1000);

  // Clamp the whole disc inside the sky rect so it never clips off a round screen.
  int r = st.radius;
  if (st.cx < sky.origin.x + r)                 st.cx = sky.origin.x + r;
  if (st.cx > sky.origin.x + sky.size.w - r)    st.cx = sky.origin.x + sky.size.w - r;
  if (st.cy < sky.origin.y + r)                 st.cy = sky.origin.y + r;
  if (st.cy > sky.origin.y + sky.size.h - r)    st.cy = sky.origin.y + sky.size.h - r;

  return st;
}

// Sky gradients: four top->bottom band colours per phase, each snapped to the
// Emery palette. Night: indigo to near-black. Dawn/dusk: violet-blue high,
// sunset orange low. Day: vivid cerulean high, picton blue low.
static void band_colors(SunState s, GColor out[4]) {
  if (s.night) {
    out[0] = GColorBlack; out[1] = GColorOxfordBlue;
    out[2] = GColorImperialPurple; out[3] = GColorIndigo;
  } else if (s.alt < 32) {                    // low sun => dawn / dusk
    out[0] = GColorLiberty; out[1] = GColorPurpureus;
    out[2] = GColorSunsetOrange; out[3] = GColorRajah;
  } else {                                    // high sun => full day
    out[0] = GColorVividCerulean; out[1] = GColorVividCerulean;
    out[2] = GColorPictonBlue; out[3] = GColorCeleste;
  }
}

// A handful of fixed star positions (fractions of the sky rect), drawn at night.
static const int STAR_X[] = { 18, 47, 72, 90, 34, 61, 12, 83 };
static const int STAR_Y[] = { 12, 22, 10, 30, 40, 45, 55, 60 };

void sky_draw(GContext *ctx, GRect sky, SunState s) {
  GColor bands[4];
  band_colors(s, bands);

  int bh = sky.size.h / 4;
  for (int i = 0; i < 4; i++) {
    int y = sky.origin.y + i * bh;
    int h = (i == 3) ? (sky.origin.y + sky.size.h - y) : bh;   // last band absorbs rounding
    graphics_context_set_fill_color(ctx, bands[i]);
    graphics_fill_rect(ctx, GRect(sky.origin.x, y, sky.size.w, h), 0, GCornerNone);
  }

  if (s.night) {
    graphics_context_set_fill_color(ctx, GColorWhite);
    for (unsigned i = 0; i < sizeof(STAR_X) / sizeof(STAR_X[0]); i++) {
      int x = sky.origin.x + sky.size.w * STAR_X[i] / 100;
      int y = sky.origin.y + sky.size.h * STAR_Y[i] / 100;
      graphics_fill_rect(ctx, GRect(x, y, 2, 2), 0, GCornerNone);
    }
  }

  GPoint c = GPoint(s.cx, s.cy);
  if (s.night) {
    // Moon: pale disc with a soft grey corona ring.
    graphics_context_set_fill_color(ctx, GColorLightGray);
    graphics_fill_circle(ctx, c, s.radius);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_fill_circle(ctx, c, s.radius - 2);
  } else {
    // Sun: warm disc with an orange corona ring.
    graphics_context_set_fill_color(ctx, GColorOrange);
    graphics_fill_circle(ctx, c, s.radius);
    graphics_context_set_fill_color(ctx, GColorYellow);
    graphics_fill_circle(ctx, c, s.radius - 2);
  }
}
