#include "sky.h"
#include "weather.h"   // the WX_* condition enum

// Collapse the ten-value condition code into three sky treatments:
//   0 = clear / few clouds  -> vivid sky, disc drawn
//   1 = cloudy              -> muted blue-grey sky, disc still drawn
//   2 = overcast and wetter -> flat grey sky, no disc, no stars
static int cloud_cover(uint8_t code) {
  if (code >= WX_OVERCAST) return 2;   // overcast, fog, drizzle, rain, snow, thunder
  if (code == WX_CLOUDY)   return 1;
  return 0;                            // WX_CLEAR, WX_FEW
}

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

SunState sun_compute(time_t now, time_t sunrise, time_t sunset, GRect sky) {
  SunState st = { .cx = 0, .cy = 0, .radius = BASE_SUN, .night = false, .alt = 100 };

  int32_t fx_1000 = 500, alt_1000 = 1000;   // normalised x and altitude, 0..1000
  int32_t y_1000  = 250;                     // normalised y (0 top, 1000 bottom)
  bool night = false;

  // Re-date sunrise and sunset onto now's calendar day as a coherent, ordered
  // pair from their clock times, so a moment before sunrise or after sunset reads
  // as night instead of collapsing the pair across midnight (which used to force
  // a day sky all night long).
  time_t sr = 0, ss = 0;
  if (sunrise != 0 && sunset != 0) {
    struct tm nt = *localtime(&now);
    struct tm rt = *localtime(&sunrise);
    struct tm a = nt; a.tm_hour = rt.tm_hour; a.tm_min = rt.tm_min; a.tm_sec = rt.tm_sec;
    sr = mktime(&a);
    struct tm st_ = *localtime(&sunset);
    struct tm b = nt; b.tm_hour = st_.tm_hour; b.tm_min = st_.tm_min; b.tm_sec = st_.tm_sec;
    ss = mktime(&b);
  }

  if (sr == 0 || ss == 0 || ss <= sr) {
    // Missing or polar sun times: a neutral high-noon disc, day sky, no crash.
    fx_1000 = 500; alt_1000 = 1000;
    y_1000  = 800 - alt_1000 * 660 / 1000;   // y = 0.80 - alt*0.66
    night = false;
  } else if (now >= sr && now <= ss) {
    // Daytime: sun sweeps left(east) to right(west), high at noon.
    fx_1000  = (int32_t)(now - sr) * 1000 / (ss - sr);
    alt_1000 = sin_pi_1000(fx_1000);
    y_1000   = 800 - alt_1000 * 660 / 1000;  // y = 0.80 - alt*0.66
    night = false;
  } else {
    // Night: a gentler moon arc from the previous sunset to the next sunrise.
    night = true;
    time_t prev_set, next_rise;
    if (now < sr) { prev_set = ss - SECS_PER_DAY; next_rise = sr; }
    else          { prev_set = ss;                next_rise = sr + SECS_PER_DAY; }
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
static void band_colors(SunState s, int cover, GColor out[4]) {
  if (cover >= 2) {
    // Overcast: a flat grey wash, no visible disc. A touch darker at night so
    // it still reads as a night sky rather than a daytime one.
    if (s.night) {
      out[0] = GColorDarkGray;  out[1] = GColorDarkGray;
      out[2] = GColorDarkGray;  out[3] = GColorBlack;
    } else {
      out[0] = GColorLightGray; out[1] = GColorLightGray;
      out[2] = GColorLightGray; out[3] = GColorLightGray;
    }
    return;
  }

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

  if (cover == 1) {
    // Cloudy: keep the disc but drain the vividness, greying the lower bands so
    // the sky reads as a muted blue-grey instead of a clear day.
    if (!s.night && s.alt >= 32) out[1] = GColorCadetBlue;   // greyed cerulean
    out[2] = GColorLightGray;
    out[3] = s.night ? GColorDarkGray : GColorLightGray;
  }
}

// A handful of fixed star positions (fractions of the sky rect), drawn at night.
static const int STAR_X[] = { 18, 47, 72, 90, 34, 61, 12, 83 };
static const int STAR_Y[] = { 12, 22, 10, 30, 40, 45, 55, 60 };

void sky_draw(GContext *ctx, GRect sky, SunState s, uint8_t code) {
  int cover = cloud_cover(code);
  GColor bands[4];
  band_colors(s, cover, bands);

  int bh = sky.size.h / 4;
  for (int i = 0; i < 4; i++) {
    int y = sky.origin.y + i * bh;
    int h = (i == 3) ? (sky.origin.y + sky.size.h - y) : bh;   // last band absorbs rounding
    graphics_context_set_fill_color(ctx, bands[i]);
    graphics_fill_rect(ctx, GRect(sky.origin.x, y, sky.size.w, h), 0, GCornerNone);
  }

  // Overcast: the sky is a flat grey wash with the sun/moon hidden behind it.
  if (cover >= 2) return;

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

// ---- foreground landscape --------------------------------------------------

// Fill a closed polygon in one colour (GPath owns a transient copy of points).
static void fill_poly(GContext *ctx, const GPoint *pts, uint32_t n, GColor col) {
  GPathInfo info = { .num_points = n, .points = (GPoint *)pts };
  GPath *p = gpath_create(&info);
  if (!p) return;
  graphics_context_set_fill_color(ctx, col);
  // gpath_draw_filled omits the left boundary column, so a polygon whose left
  // edge sits at x=0 leaves column 0 unpainted. Shift the whole path 1px left:
  // the left edge moves off-screen (column 0 fills) and the right edge, already
  // a pixel past the screen at L+W, still covers the last column.
  gpath_move_to(p, GPoint(-1, 0));
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

// Scenery palette, chosen by the same day / dusk / night / overcast phases the
// sky uses so the ground reads as part of the same moment.
typedef struct { GColor ridge, mountain, cap, hills, tree, trunk, shadow; } SceneColors;

static SceneColors scene_colors(SunState s, int cover) {
  SceneColors c;
  if (cover >= 2) {                 // overcast: flat greys, everything muted
    c.ridge = GColorLightGray; c.mountain = GColorDarkGray; c.cap = GColorWhite;
    c.hills = GColorDarkGray;  c.tree = GColorBlack; c.trunk = GColorBlack;
    c.shadow = GColorBlack;
  } else if (s.night) {             // night: blue-black silhouettes
    c.ridge = GColorOxfordBlue; c.mountain = GColorOxfordBlue; c.cap = GColorLightGray;
    c.hills = GColorBlack; c.tree = GColorBlack; c.trunk = GColorBlack;
    c.shadow = GColorBlack;
  } else if (s.alt < 32) {          // dawn / dusk: darker, warmer ground
    c.ridge = GColorLightGray; c.mountain = GColorDarkGray; c.cap = GColorWhite;
    c.hills = GColorArmyGreen; c.tree = GColorDarkGreen; c.trunk = GColorBulgarianRose;
    c.shadow = GColorArmyGreen;
  } else {                          // full day
    c.ridge = GColorLightGray; c.mountain = GColorDarkGray; c.cap = GColorWhite;
    c.hills = GColorKellyGreen; c.tree = GColorDarkGreen; c.trunk = GColorWindsorTan;
    c.shadow = GColorArmyGreen;
  }
  return c;
}

// One tree: ground shadow (offset by the sun), trunk, then a three-lobe canopy.
// tx/groundY are the trunk base; fr scales the whole tree. sunx_1000 is the sun's
// horizontal position across the scene (0 = left/sunrise, 1000 = right/sunset).
static void draw_tree(GContext *ctx, int tx, int groundY, int fr,
                      int sunx_1000, bool cast_shadow, SceneColors c) {
  if (cast_shadow) {
    // shadowDir = 0.5 - sunX: sun on the left -> positive -> shadow falls right.
    int dir = 500 - sunx_1000;                 // -500..500
    int adir = dir < 0 ? -dir : dir;
    int len = adir * 24 / 10;                   // grows as the sun gets low
    if (len < 350)  len = 350;
    if (len > 1400) len = 1400;
    int reach = fr * 26 / 10 * len / 1000;      // how far the shadow reaches
    int sh   = fr * 6 / 10;                      // a flat ground ellipse
    int back = fr * 5 / 10;                      // small tuck under the trunk base
    int sgn  = (dir >= 0) ? 1 : -1;             // shadow falls away from the sun
    int far  = tx + sgn * reach;                 // shadow tip
    int near = tx - sgn * back;                  // anchored just behind the base
    int left  = (far < near) ? far : near;
    int width = (far < near) ? (near - far) : (far - near);
    int sy = groundY - sh / 2;                   // centred on the ground at the base
    graphics_context_set_fill_color(ctx, c.shadow);
    graphics_fill_rect(ctx, GRect(left, sy, width, sh), sh / 2, GCornersAll);
  }

  int th = fr * 110 / 100;                       // trunk height
  int tw = fr * 28 / 100; if (tw < 2) tw = 2;    // trunk width
  graphics_context_set_fill_color(ctx, c.trunk);
  graphics_fill_rect(ctx, GRect(tx - tw / 2, groundY - th, tw, th), tw / 2, GCornersAll);

  int fy = groundY - th;                         // canopy sits on the trunk top
  graphics_context_set_fill_color(ctx, c.tree);
  graphics_fill_circle(ctx, GPoint(tx - fr * 70 / 100, fy + fr * 10 / 100), fr * 70 / 100);
  graphics_fill_circle(ctx, GPoint(tx + fr * 70 / 100, fy + fr * 10 / 100), fr * 70 / 100);
  graphics_fill_circle(ctx, GPoint(tx, fy - fr * 40 / 100), fr);
}

// Static precipitation (no animation): streak/flake positions come from a small
// hash of the cell so they scatter naturally and stay put between redraws.
static void draw_rain(GContext *ctx, GRect r, int density, GColor col) {
  graphics_context_set_stroke_color(ctx, col);
  int stepx = (density >= 2) ? 12 : (density == 1 ? 18 : 26);   // heavy/med/light
  int len   = (density >= 2) ? 11 : (density == 1 ? 9 : 7);
  for (int x = r.origin.x - 6; x < r.origin.x + r.size.w; x += stepx) {
    for (int y = r.origin.y - 4; y < r.origin.y + r.size.h; y += 20) {
      int h = x * 31 + y * 17;
      int px = x + (h % stepx), py = y + (h % 15);
      graphics_draw_line(ctx, GPoint(px, py), GPoint(px - 3, py + len));  // slanted
    }
  }
}

static void draw_snow(GContext *ctx, GRect r, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  for (int x = r.origin.x; x < r.origin.x + r.size.w; x += 22) {
    for (int y = r.origin.y - 4; y < r.origin.y + r.size.h; y += 20) {
      int h = x * 13 + y * 29;
      graphics_fill_circle(ctx, GPoint(x + (h % 20), y + (h % 16)), 2);
    }
  }
}

// A lightning bolt in the upper sky, kept to the right so it clears the text.
static void draw_bolt(GContext *ctx, GRect r) {
  int bx = r.origin.x + r.size.w * 58 / 100;
  int by = r.origin.y + r.size.h * 12 / 100;
  GPoint bolt[] = {
    {bx,      by},       {bx - 9, by + 22}, {bx - 1, by + 22},
    {bx - 11, by + 44},  {bx + 8, by + 16}, {bx,     by + 16}, {bx + 8, by},
  };
  fill_poly(ctx, bolt, sizeof(bolt) / sizeof(bolt[0]), GColorYellow);
}

void scene_draw(GContext *ctx, GRect r, SunState s, uint8_t code) {
  int cover = (code >= WX_OVERCAST) ? 2 : (code == WX_CLOUDY ? 1 : 0);
  SceneColors c = scene_colors(s, cover);

  int W = r.size.w, L = r.origin.x;
  int band = r.size.h * 46 / 100;
  int top  = r.origin.y + r.size.h - band;       // horizon line
  int bot  = r.origin.y + r.size.h;

  // Distant ridge (drawn first, sits behind the main range).
  GPoint ridge[] = {
    {L,             top + band * 20 / 100}, {L + W * 15 / 100, top + band * 10 / 100},
    {L + W * 30/100, top + band * 16 / 100}, {L + W * 45 / 100, top + band *  6 / 100},
    {L + W * 60/100, top + band * 14 / 100}, {L + W * 78 / 100, top + band *  8 / 100},
    {L + W,         top + band * 18 / 100}, {L + W, bot}, {L, bot},
  };
  fill_poly(ctx, ridge, sizeof(ridge) / sizeof(ridge[0]), c.ridge);

  // Main mountain range, highest peak just left of centre.
  int px = L + W * 48 / 100, py = top + band * 2 / 100;
  GPoint mtn[] = {
    {L,             top + band * 35 / 100}, {L + W * 20 / 100, top + band * 16 / 100},
    {L + W * 35/100, top + band * 24 / 100}, {px, py},
    {L + W * 62/100, top + band * 20 / 100}, {L + W * 80 / 100, top + band * 12 / 100},
    {L + W,         top + band * 30 / 100}, {L + W, bot}, {L, bot},
  };
  fill_poly(ctx, mtn, sizeof(mtn) / sizeof(mtn[0]), c.mountain);

  // Snow cap on the main peak when it is actually snowing.
  if (code == WX_SNOW) {
    GPoint cap[] = { {px, py}, {px - W * 4 / 100, py + band * 10 / 100},
                     {px + W * 4 / 100, py + band * 10 / 100} };
    fill_poly(ctx, cap, 3, c.cap);
  }

  // Green hills in front, the ground the trees stand on.
  GPoint hills[] = {
    {L,             top + band * 62 / 100}, {L + W * 18 / 100, top + band * 50 / 100},
    {L + W * 40/100, top + band * 60 / 100}, {L + W * 60 / 100, top + band * 52 / 100},
    {L + W * 82/100, top + band * 62 / 100}, {L + W, top + band * 56 / 100},
    {L + W, bot}, {L, bot},
  };
  fill_poly(ctx, hills, sizeof(hills) / sizeof(hills[0]), c.hills);

  // Shadows are cast only when the sun is actually up (not night, not overcast).
  bool cast = !s.night && cover < 2;
  int sunx = (r.size.w > 0) ? (s.cx - r.origin.x) * 1000 / r.size.w : 500;
  if (sunx < 0) sunx = 0;
  if (sunx > 1000) sunx = 1000;

  draw_tree(ctx, L + W * 19 / 100, top + band * 74 / 100, band * 13 / 100, sunx, cast, c);
  draw_tree(ctx, L + W * 80 / 100, top + band * 68 / 100, band * 20 / 100, sunx, cast, c);

  // Precipitation in front of the scenery. Rain is a saturated blue by day and a
  // pale blue at night; snow is white flakes; thunder adds a bolt over the rain.
  GColor rain = s.night ? GColorCeleste : GColorVividCerulean;
  switch (code) {
    case WX_DRIZZLE: draw_rain(ctx, r, 0, rain); break;
    case WX_RAIN:    draw_rain(ctx, r, 1, rain); break;
    case WX_HEAVY:   draw_rain(ctx, r, 2, rain); break;
    case WX_THUNDER: draw_rain(ctx, r, 2, rain); draw_bolt(ctx, r); break;
    case WX_SNOW:    draw_snow(ctx, r, GColorWhite); break;
    default: break;
  }
}
