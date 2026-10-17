#include <pebble.h>
#include "data.h"
#include "message_keys.auto.h"

// Aura Cityscape Weather: a small town under a layered sky.
// A small stylised town under a layered sky. The scene itself carries the data:
//   time of day  -> the sun or moon rides its real arc; the sky and every layer
//                   (far hills, town, ground) swap palette: night, dawn, day,
//                   golden, dusk; windows light up after dark
//   condition    -> stepped pixel clouds (two parallax speeds), a thick deck when
//                   overcast, fog, rain, snow caps on the roofs, lightning
//   temperature  -> the thermometer tower: its tube is filled to the current
//                   temperature in the Aura ramp colour
//   wind         -> the pennant on the tower: limp when calm, flying when strong
// Below the scene sits a "paper" panel (light by day, dark by night) with the
// numbers. Pages: UP / DOWN. Hero, Hourly (a grid of little windows, one per
// hour), Daily (the week as a skyline: building height is that day's high).
// SELECT refreshes (mock mode: steps to the next scripted scenario; long
// SELECT steps back).

// ---- knobs: the things to tweak first --------------------------------------
// The first three are build-time switches. The shipping defaults are below; to preview
// the scripted scenarios, build with the environment variables read by wscript
// (AURA_CITY_MOCK=1 AURA_CITY_TAG=1 AURA_CITY_SCENARIO=3 pebble build), no source edit.
#ifndef MOCK
#define MOCK                0    // 1 = scripted scenarios and a fake clock, no phone needed; 0 = real data
#endif
#ifndef MOCK_SCENARIO
#define MOCK_SCENARIO       0    // scenario shown at launch (0..7, see data.c)
#endif
#ifndef SHOW_MOCK_TAG
#define SHOW_MOCK_TAG       0    // print the scenario name in the scene corner (mock only)
#endif
#ifndef DEBUG_LOG
#define DEBUG_LOG           0    // 1 = log heap use and draw time (AURA_CITY_DEBUG=1)
#endif
#define STALE_AFTER_MIN   120    // show "Xh ago" in the scene corner once the forecast is older than this
#define SCENE_PCT          60    // height of the scene on the hero, % of the screen
#define DAWN_BEFORE_MIN    30    // dawn palette starts this long before sunrise...
#define DAWN_AFTER_MIN     60    // ...and ends this long after it
#define GOLDEN_BEFORE_MIN  75    // golden palette: the last N minutes before sunset
#define DUSK_AFTER_MIN     50    // dusk palette: N minutes after sunset
#define ANIM_MS          1000    // animation tick while the hero is animating; 0 = static
#define SHAKE_TICKS         8    // ticks of motion after a wrist shake (and at launch); 0 = never animate
#define CLOUD_DRIFT_PX      1    // far clouds slide this many px per tick (near ones twice)
#define RAIN_FALL_PX        5    // rain streaks fall this many px per tick
#define THERMO_MIN_C       -5    // temperature at the bottom of the thermometer tube
#define THERMO_MAX_C       40    // temperature at the top of the tube
#define TOWER_H            76    // thermometer tower height, px
#define LIT_WINDOWS_PCT    66    // share of windows that light up after dark
// -----------------------------------------------------------------------------

enum { PAGE_HERO = 0, PAGE_HOURLY, PAGE_DAILY, PAGE_N };

static Window   *s_window;
static Layer    *s_canvas;
static AppTimer *s_timer;
static int s_anim_left;   // ticks of motion left; a wrist shake refills it
static int       s_page = PAGE_HERO;
static int       s_drift;
static bool      s_focused = true;

static GFont s_font_temp;   // hero temperature (48)
static GFont s_font_text;   // values (20)
static GFont s_font_small;  // labels (16)
static GFont s_font_tiny;   // captions (14)
static GFont s_font_wx;     // condition glyph, hero (30)
static GFont s_font_wx_sm;  // condition glyph, tiles (20)

// ---- helpers ---------------------------------------------------------------

static void text_at(GContext *ctx, const char *s, GFont f, GRect box,
                    GColor col, GTextAlignment align) {
  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, s, f, box, GTextOverflowModeTrailingEllipsis, align, NULL);
}

static void text_outline(GContext *ctx, const char *s, GFont f, GRect box, GColor col,
                         GTextAlignment align) {
  static const int dx[] = { -1, 1, 0, 0, -1, 1, -1, 1 }, dy[] = { 0, 0, -1, 1, -1, -1, 1, 1 };
  for (int i = 0; i < 8; i++) {
    text_at(ctx, s, f, GRect(box.origin.x + dx[i], box.origin.y + dy[i], box.size.w, box.size.h),
            GColorBlack, align);
  }
  text_at(ctx, s, f, box, col, align);
}

static void fill_poly(GContext *ctx, const GPoint *pts, uint32_t n, GColor col) {
  GPathInfo info = { .num_points = n, .points = (GPoint *)pts };
  GPath *p = gpath_create(&info);
  if (!p) return;
  graphics_context_set_fill_color(ctx, col);
  gpath_move_to(p, GPoint(-1, 0));
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

static uint32_t hash2(int a, int b) {
  uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

static const uint8_t BAYER[4][4] = {
  { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
};

static int sky_grade(uint8_t code) {   // 0 clear, 1 muted, 2 grey, 3 dark, 4 fog
  if (code == WX_CLEAR || code == WX_FEW) return 0;
  if (code == WX_CLOUDY) return 1;
  if (code == WX_FOG) return 4;
  if (code == WX_HEAVY || code == WX_THUNDER) return 3;
  return 2;
}

// ---- the look: one palette per (phase, condition) --------------------------

typedef struct {
  GColor sky[4];                 // top to horizon
  GColor far, mid, near, tree;   // hills, town, ground, pines
  GColor lit, unlit;             // window colours
  GColor panel, ink, dim;        // the data panel
} Look;

static Look look_for(SkyPhase ph, uint8_t code) {
  Look k;
  int g = sky_grade(code);
  bool dark = (ph == PH_NIGHT || ph == PH_DUSK);
  k.lit = GColorPastelYellow;
  switch (ph) {
    case PH_DAWN:
      k.sky[0] = GColorLiberty; k.sky[1] = GColorPurpureus; k.sky[2] = GColorMelon; k.sky[3] = GColorRajah;
      k.far = GColorMelon; k.mid = GColorPurpureus; k.near = GColorImperialPurple; k.tree = GColorBlack;
      k.unlit = GColorRoseVale; k.panel = GColorMelon; k.ink = GColorBlack; k.dim = GColorImperialPurple;
      break;
    case PH_GOLDEN:
      k.sky[0] = GColorPurpureus; k.sky[1] = GColorSunsetOrange; k.sky[2] = GColorRajah; k.sky[3] = GColorPastelYellow;
      k.far = GColorRoseVale; k.mid = GColorImperialPurple; k.near = GColorBulgarianRose; k.tree = GColorBlack;
      k.unlit = GColorBulgarianRose; k.panel = GColorRajah; k.ink = GColorBlack; k.dim = GColorBulgarianRose;
      break;
    case PH_DUSK:
      k.sky[0] = GColorOxfordBlue; k.sky[1] = GColorIndigo; k.sky[2] = GColorFashionMagenta; k.sky[3] = GColorOrange;
      k.far = GColorIndigo; k.mid = GColorImperialPurple; k.near = GColorBlack; k.tree = GColorBlack;
      k.unlit = GColorBlack; k.panel = GColorOxfordBlue; k.ink = GColorWhite; k.dim = GColorLightGray;
      break;
    case PH_NIGHT:
      k.sky[0] = GColorBlack; k.sky[1] = GColorOxfordBlue; k.sky[2] = GColorOxfordBlue; k.sky[3] = GColorDukeBlue;
      k.far = GColorIndigo; k.mid = GColorOxfordBlue; k.near = GColorBlack; k.tree = GColorBlack;
      k.unlit = GColorBlack; k.panel = GColorBlack; k.ink = GColorWhite; k.dim = GColorLightGray;
      break;
    default:
      k.sky[0] = GColorPictonBlue; k.sky[1] = GColorPictonBlue; k.sky[2] = GColorCeleste; k.sky[3] = GColorCeleste;
      k.far = GColorBabyBlueEyes; k.mid = GColorLiberty; k.near = GColorMayGreen; k.tree = GColorDarkGreen;
      k.unlit = GColorBabyBlueEyes; k.panel = GColorWhite; k.ink = GColorBlack; k.dim = GColorDarkGray;
      break;
  }
  if (g == 1) {                          // cloudy: drain the sky a little
    k.sky[2] = dark ? k.sky[2] : GColorBabyBlueEyes;
    if (ph == PH_DAY) { k.sky[0] = GColorCadetBlue; k.sky[1] = GColorPictonBlue; k.sky[3] = GColorBabyBlueEyes; }
  }
  if (g >= 2) {                          // grey weather
    if (dark) {
      k.sky[0] = GColorBlack; k.sky[1] = GColorBlack; k.sky[2] = GColorDarkGray;
      k.sky[3] = (ph == PH_DUSK) ? GColorImperialPurple : GColorDarkGray;
      k.far = GColorDarkGray; k.mid = GColorOxfordBlue; k.near = GColorBlack; k.tree = GColorBlack;
      k.unlit = GColorBlack;
    } else {
      k.sky[0] = GColorCadetBlue; k.sky[1] = GColorLightGray; k.sky[2] = GColorLightGray;
      k.sky[3] = (ph == PH_GOLDEN) ? GColorRajah : (ph == PH_DAWN ? GColorMelon : GColorWhite);
      k.far = GColorLightGray; k.mid = GColorDarkGray; k.near = GColorDarkGray; k.tree = GColorBlack;
      k.unlit = GColorLightGray; k.panel = GColorLightGray; k.ink = GColorBlack; k.dim = GColorDarkGray;
      if (ph != PH_DAY) { k.far = GColorLightGray; k.sky[1] = GColorDarkGray; k.sky[2] = GColorLightGray; }
    }
  }
  if (g == 3) {                          // storm: heavy and dark
    k.sky[0] = GColorBlack; k.sky[1] = GColorDarkGray; k.sky[2] = GColorDarkGray;
    k.sky[3] = dark ? GColorDarkGray : GColorLightGray;
    k.far = GColorDarkGray; k.mid = GColorBlack; k.near = GColorBlack; k.tree = GColorBlack;
    k.unlit = GColorBlack;
    if (!dark) { k.panel = GColorDarkGray; k.ink = GColorWhite; k.dim = GColorLightGray; }
  }
  if (g == 4) {                          // fog: low contrast, bright
    if (!dark) {
      k.sky[0] = GColorLightGray; k.sky[1] = GColorLightGray; k.sky[2] = GColorWhite; k.sky[3] = GColorWhite;
      k.far = GColorWhite; k.mid = GColorCadetBlue; k.near = GColorDarkGray; k.tree = GColorBlack;
      k.unlit = GColorWhite; k.panel = GColorLightGray; k.ink = GColorBlack; k.dim = GColorDarkGray;
    } else {
      k.sky[0] = GColorBlack; k.sky[1] = GColorDarkGray; k.sky[2] = GColorDarkGray; k.sky[3] = GColorLightGray;
      k.far = GColorDarkGray; k.mid = GColorOxfordBlue; k.near = GColorBlack;
    }
  }
  if (code == WX_SNOW && !dark) {
    k.near = GColorWhite; k.tree = GColorDarkGray; k.panel = GColorWhite; k.ink = GColorBlack; k.dim = GColorDarkGray;
  }
  if (code == WX_SNOW && dark) { k.near = GColorLightGray; k.tree = GColorBlack; }
  return k;
}

// Ordered-dither band painter: n solid bands with dithered seams.
static void sky_paint(GContext *ctx, GRect r, const GColor *st, int n) {
  int bh = r.size.h / n;
  for (int i = 0; i < n; i++) {
    int y0 = r.origin.y + i * bh;
    int h = (i == n - 1) ? (r.origin.y + r.size.h - y0) : bh;
    graphics_context_set_fill_color(ctx, st[i]);
    graphics_fill_rect(ctx, GRect(r.origin.x, y0, r.size.w, h), 0, GCornerNone);
  }
  const int rows = 3;                          // seam = rows*2 cell rows, 2px each
  if (bh < rows * 4 + 4) return;
  for (int i = 1; i < n; i++) {
    int top = r.origin.y + i * bh - rows * 2;
    graphics_context_set_fill_color(ctx, st[i - 1]);
    graphics_fill_rect(ctx, GRect(r.origin.x, top, r.size.w, rows * 4), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, st[i]);
    for (int cy = 0; cy < rows * 2; cy++) {
      int level = (cy * 16 + 8) / (rows * 2);
      for (int cx = 0; cx < r.size.w / 2; cx++) {
        if (BAYER[cy & 3][cx & 3] < level) {
          graphics_fill_rect(ctx, GRect(r.origin.x + cx * 2, top + cy * 2, 2, 2), 0, GCornerNone);
        }
      }
    }
  }
}

// ---- sun, moon, stars ------------------------------------------------------

static void draw_sun(GContext *ctx, GPoint c, int r, bool low) {
  graphics_context_set_stroke_color(ctx, low ? GColorChromeYellow : GColorYellow);
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 0; i < 8; i++) {                // short rays
    int a = TRIG_MAX_ANGLE * i / 8;
    int x0 = c.x + (r + 3) * sin_lookup(a) / TRIG_MAX_RATIO;
    int y0 = c.y - (r + 3) * cos_lookup(a) / TRIG_MAX_RATIO;
    int x1 = c.x + (r + 7) * sin_lookup(a) / TRIG_MAX_RATIO;
    int y1 = c.y - (r + 7) * cos_lookup(a) / TRIG_MAX_RATIO;
    graphics_draw_line(ctx, GPoint(x0, y0), GPoint(x1, y1));
  }
  graphics_context_set_stroke_width(ctx, 1);
  graphics_context_set_fill_color(ctx, GColorOrange);
  graphics_fill_circle(ctx, c, r);
  graphics_context_set_fill_color(ctx, low ? GColorChromeYellow : GColorIcterine);
  graphics_fill_circle(ctx, c, r - 2);
}

static void draw_moon(GContext *ctx, GPoint c, int r, int lit_pct, bool waxing, GColor sky) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, c, r);
  if (lit_pct < 97) {
    int dx = lit_pct * 2 * r / 100;
    graphics_context_set_fill_color(ctx, sky);
    graphics_fill_circle(ctx, GPoint(c.x + (waxing ? -dx : dx), c.y), r);
  }
}

static const uint8_t STAR_X[] = { 8, 27, 49, 70, 91, 18, 60, 83, 38, 96, 4, 66 };
static const uint8_t STAR_Y[] = { 14, 8, 22, 12, 28, 38, 42, 52, 56, 64, 58, 32 };

static void draw_stars(GContext *ctx, GRect r, int count) {
  for (int i = 0; i < count && i < (int)sizeof(STAR_X); i++) {
    int x = r.origin.x + r.size.w * STAR_X[i] / 100;
    int y = r.origin.y + r.size.h * STAR_Y[i] / 100 * 70 / 100;
    graphics_context_set_fill_color(ctx, (i % 3 == 0) ? GColorCeleste : GColorWhite);
    graphics_fill_rect(ctx, GRect(x, y, (i % 4 == 0) ? 2 : 1, (i % 4 == 0) ? 2 : 1), 0, GCornerNone);
  }
}

// ---- clouds: stepped pixel puffs --------------------------------------------

static void draw_pcloud(GContext *ctx, int x, int y, int w, GColor main, GColor shade) {
  int h = w * 40 / 100;
  GRect top  = GRect(x + w * 30 / 100, y,               w * 30 / 100, h * 40 / 100);
  GRect mid  = GRect(x + w * 12 / 100, y + h * 30 / 100, w * 66 / 100, h * 40 / 100);
  GRect base = GRect(x,                y + h * 60 / 100, w,            h * 40 / 100);
  for (int pass = 0; pass < 2; pass++) {
    int oy = (pass == 0) ? 2 : 0;
    graphics_context_set_fill_color(ctx, pass == 0 ? shade : main);
    graphics_fill_rect(ctx, GRect(top.origin.x, top.origin.y + oy, top.size.w, top.size.h), 2, GCornersAll);
    graphics_fill_rect(ctx, GRect(mid.origin.x, mid.origin.y + oy, mid.size.w, mid.size.h), 3, GCornersAll);
    graphics_fill_rect(ctx, GRect(base.origin.x, base.origin.y + oy, base.size.w, base.size.h), 3, GCornersAll);
  }
}

static int drift_x(int base, int w, int W, int speed) {
  int span = W + w + 20;
  return (base + s_drift * CLOUD_DRIFT_PX * speed) % span - w - 10;
}

static void draw_clouds(GContext *ctx, GRect r, uint8_t code, SkyPhase ph, Look *k) {
  int W = r.size.w, H = r.size.h;
  int g = sky_grade(code);
  bool dark = (ph == PH_NIGHT || ph == PH_DUSK);
  GColor main, shade, far_main;
  if (g >= 3)       { main = GColorDarkGray; shade = GColorBlack; far_main = GColorBlack; }
  else if (dark)    { main = GColorDarkGray; shade = GColorBlack; far_main = GColorOxfordBlue; }
  else if (g >= 2)  { main = GColorLightGray; shade = GColorDarkGray; far_main = GColorWhite; }
  else if (ph == PH_GOLDEN || ph == PH_DAWN) { main = GColorMelon; shade = GColorSunsetOrange; far_main = GColorPastelYellow; }
  else              { main = GColorWhite; shade = GColorBabyBlueEyes; far_main = GColorCeleste; }
  (void)k;
  switch (code) {
    case WX_CLEAR: case WX_FOG: break;
    case WX_FEW:
      draw_pcloud(ctx, drift_x(W * 15 / 100, 30, W, 1), H * 16 / 100, 30, far_main, far_main);
      draw_pcloud(ctx, drift_x(W * 30 / 100, 58, W, 2), H * 24 / 100, 58, main, shade);
      draw_pcloud(ctx, drift_x(W * 80 / 100, 40, W, 2), H * 46 / 100, 40, main, shade);
      break;
    case WX_CLOUDY:
      draw_pcloud(ctx, drift_x(W * 5 / 100, 34, W, 1), H * 12 / 100, 34, far_main, far_main);
      draw_pcloud(ctx, drift_x(W * 60 / 100, 38, W, 1), H * 30 / 100, 38, far_main, far_main);
      draw_pcloud(ctx, drift_x(W * 20 / 100, 66, W, 2), H * 22 / 100, 66, main, shade);
      draw_pcloud(ctx, drift_x(W * 62 / 100, 76, W, 2), H * 40 / 100, 76, main, shade);
      draw_pcloud(ctx, drift_x(W * 98 / 100, 50, W, 3), H * 8 / 100, 50, main, shade);
      break;
    default: {                                 // overcast and wetter: two rows of big puffs
      for (int i = 0; i < 4; i++) {
        draw_pcloud(ctx, drift_x(i * (W / 3) - 20, 84, W, 1), H * (6 + (i & 1) * 5) / 100, 84, far_main, shade);
      }
      for (int i = 0; i < 4; i++) {
        draw_pcloud(ctx, drift_x(i * (W / 3) + 10, 92, W, 2), H * (20 + (i & 1) * 6) / 100, 92, main, shade);
      }
      break;
    }
  }
}

static void draw_fog(GContext *ctx, GRect r, SkyPhase ph) {
  GColor col = (ph == PH_NIGHT || ph == PH_DUSK) ? GColorLightGray : GColorWhite;
  graphics_context_set_fill_color(ctx, col);
  int H = r.size.h, W = r.size.w, y0 = H * 30 / 100, span = H - y0;
  int off = s_drift & 7;
  for (int y = y0; y < H; y += 2) {
    int dens = (y - y0) * 14 / span;
    for (int x = -8; x < W; x += 2) {
      if (BAYER[(y >> 1) & 3][((x + off) >> 1) & 3] < dens) {
        graphics_fill_rect(ctx, GRect(x + off, y, 2, 2), 0, GCornerNone);
      }
    }
  }
}

static void draw_rain(GContext *ctx, GRect r, int density, GColor col) {
  graphics_context_set_stroke_color(ctx, col);
  int stepx = density >= 2 ? 11 : (density == 1 ? 16 : 24);
  int len   = density >= 2 ? 11 : (density == 1 ? 9 : 6);
  int fall  = (s_drift * RAIN_FALL_PX) % 20;
  for (int x = r.origin.x - 6; x < r.origin.x + r.size.w; x += stepx) {
    for (int y = r.origin.y - 20; y < r.origin.y + r.size.h; y += 20) {
      int h = (int)(hash2(x, y) & 0xFFFF);
      int px = x + (h % stepx), py = y + (h % 15) + fall;
      if (py < r.origin.y || py + len > r.origin.y + r.size.h) continue;
      graphics_draw_line(ctx, GPoint(px, py), GPoint(px - 3, py + len));
    }
  }
}

static void draw_snow(GContext *ctx, GRect r) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  int step = 18, fall = (s_drift * 2) % step;
  for (int gx = r.origin.x - step; gx < r.origin.x + r.size.w + step; gx += step) {
    for (int gy = r.origin.y - step; gy < r.origin.y + r.size.h; gy += step) {
      uint32_t hh = hash2(gx, gy);
      if ((hh & 7u) == 0u) continue;
      int jx = (int)(hh % (uint32_t)step) + ((s_drift & 3) - 1) * ((hh >> 5) & 1);
      int py = gy + (int)((hh >> 8) % (uint32_t)step) + fall;
      if (py < r.origin.y + 2 || py > r.origin.y + r.size.h - 2) continue;
      graphics_fill_circle(ctx, GPoint(gx + jx, py), (((hh >> 17) & 3u) == 0u) ? 1 : ((((hh >> 19) & 7u) == 0u) ? 3 : 2));
    }
  }
}

static void draw_bolt(GContext *ctx, GRect r) {
  int bx = r.origin.x + r.size.w * 38 / 100, by = r.origin.y + r.size.h * 22 / 100;
  GPoint bolt[] = {
    {bx, by}, {bx - 9, by + 22}, {bx - 1, by + 22},
    {bx - 11, by + 46}, {bx + 8, by + 16}, {bx, by + 16}, {bx + 8, by},
  };
  fill_poly(ctx, bolt, sizeof(bolt) / sizeof(bolt[0]), GColorYellow);
}

// ---- the town --------------------------------------------------------------

typedef struct { uint8_t x, w, h; } Bld;       // x in % of width, w and h in px
static const Bld TOWN[] = {
  { 1, 16, 34 }, { 9, 14, 48 }, { 16, 18, 30 }, { 26, 12, 40 }, { 33, 20, 56 }, { 46, 14, 36 },
  { 53, 16, 44 }, { 62, 12, 30 }, { 69, 18, 50 }, { 80, 14, 38 }, { 88, 16, 44 }, { 96, 12, 28 },
};

static void draw_town(GContext *ctx, GRect r, int base, Look *k, bool lights, bool snow, int hscale) {
  int W = r.size.w;
  for (unsigned i = 0; i < sizeof(TOWN) / sizeof(TOWN[0]); i++) {
    int bx = r.origin.x + W * TOWN[i].x / 100, bw = TOWN[i].w, bh = TOWN[i].h * hscale / 100;
    int by = base - bh;
    graphics_context_set_fill_color(ctx, k->mid);
    graphics_fill_rect(ctx, GRect(bx, by, bw, bh), 0, GCornerNone);
    if (i == 4) graphics_fill_rect(ctx, GRect(bx + bw / 2 - 1, by - 8, 2, 8), 0, GCornerNone);  // antenna
    for (int wy = by + 4; wy + 3 < base - 2; wy += 6) {
      for (int wx = bx + 3; wx + 2 < bx + bw - 2; wx += 5) {
        bool lit = lights && (int)(hash2(wx, wy) % 100) < LIT_WINDOWS_PCT;
        graphics_context_set_fill_color(ctx, lit ? k->lit : k->unlit);
        graphics_fill_rect(ctx, GRect(wx, wy, 2, 3), 0, GCornerNone);
      }
    }
    if (snow) {
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_rect(ctx, GRect(bx - 1, by - 2, bw + 2, 3), 1, GCornersAll);
    }
  }
}

static void draw_far_hills(GContext *ctx, GRect r, int base, Look *k) {
  int W = r.size.w, L = r.origin.x;
  GPoint hills[] = {
    {L, base - 14}, {L + W * 12 / 100, base - 26}, {L + W * 28 / 100, base - 16}, {L + W * 44 / 100, base - 32},
    {L + W * 60 / 100, base - 18}, {L + W * 78 / 100, base - 28}, {L + W, base - 14}, {L + W, base}, {L, base},
  };
  fill_poly(ctx, hills, sizeof(hills) / sizeof(hills[0]), k->far);
}

static void draw_pine(GContext *ctx, int x, int base, int s, GColor col, bool snow) {
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_rect(ctx, GRect(x - 1, base - s / 4, 3, s / 4), 0, GCornerNone);
  for (int t = 0; t < 3; t++) {
    int w = s * (60 - t * 12) / 100, y = base - s / 4 - (t + 1) * s / 4;
    GPoint tri[] = { {x, y - s / 4}, {x - w / 2, y + s / 4}, {x + w / 2, y + s / 4} };
    fill_poly(ctx, tri, 3, col);
    if (snow) { GPoint cap[] = { {x, y - s / 4}, {x - w / 5, y - s / 12}, {x + w / 5, y - s / 12} }; fill_poly(ctx, cap, 3, GColorWhite); }
  }
}

static bool has_data(void);

// The thermometer tower: tube filled to the temperature, pennant for the wind.
static void draw_tower(GContext *ctx, int x, int base, Look *k) {
  int tw = 16, th = TOWER_H;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(x - 1, base - th - 1, tw + 2, th + 2), 3, GCornersTop);
  graphics_context_set_fill_color(ctx, k->mid);
  graphics_fill_rect(ctx, GRect(x, base - th, tw, th), 3, GCornersTop);
  int tube_h = th - 22, tube_x = x + tw / 2 - 2, tube_y = base - th + 8;
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, GRect(tube_x, tube_y, 5, tube_h), 2, GCornersAll);
  int t = g_wx.is_metric ? g_wx.temp : (g_wx.temp - 32) * 5 / 9;
  int span = THERMO_MAX_C - THERMO_MIN_C;
  int fill = (t - THERMO_MIN_C) * tube_h / span;
  if (fill < 3) fill = 3;
  if (fill > tube_h) fill = tube_h;
  if (!has_data()) fill = 0;                  // nothing to show yet: an empty tube
  GColor tc = temp_ink(g_wx.temp, g_wx.is_metric);
  graphics_context_set_fill_color(ctx, tc);
  if (fill > 0) graphics_fill_rect(ctx, GRect(tube_x + 1, tube_y + tube_h - fill, 3, fill), 1, GCornersAll);
  graphics_fill_circle(ctx, GPoint(tube_x + 2, tube_y + tube_h + 5), 6);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  for (int i = 0; i <= 4; i++) {              // tick marks
    int ty = tube_y + tube_h - i * tube_h / 4;
    graphics_draw_line(ctx, GPoint(tube_x + 7, ty), GPoint(tube_x + 11, ty));
  }
  // pennant: droops when calm, streams out flat above ~40 km/h
  int ptop = base - th - 14;
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_draw_line(ctx, GPoint(x + tw / 2, base - th), GPoint(x + tw / 2, ptop));
  int wind = g_wx.wind_speed; if (!g_wx.is_metric) wind = wind * 16 / 10;
  int droop = 12 - wind * 12 / 40; if (droop < 0) droop = 0;
  int len = 8 + (wind > 40 ? 40 : wind) / 4;
  GPoint flag[] = { {x + tw / 2, ptop}, {x + tw / 2 + len, ptop + droop / 2 + 1}, {x + tw / 2, ptop + 6 + droop / 3} };
  fill_poly(ctx, flag, 3, wind >= 50 ? GColorRed : GColorOrange);
}

static void draw_scene(GContext *ctx, GRect r, bool full, Look *k, SkyPhase ph, uint8_t code) {
  int W = r.size.w, H = r.size.h;
  SunPos sp = data_sun_pos(data_now());
  int g = sky_grade(code);
  bool dark = (ph == PH_NIGHT || ph == PH_DUSK);
  int gy = r.origin.y + H - 22;                // ground line

  sky_paint(ctx, r, k->sky, 4);
  if (sp.night && g <= 1) draw_stars(ctx, r, g == 0 ? 12 : 5);
  else if (ph == PH_DUSK && g == 0) draw_stars(ctx, r, 4);

  if (g < 2) {
    int rad = (sp.night ? 10 : 12) * (620 + 380 * sp.alt_1000 / 1000) / 1000;
    if (!full) rad = rad * 3 / 4;
    int cx = rad + 8 + (W - 2 * (rad + 8)) * sp.frac_1000 / 1000;
    int arc_max = full ? gy - 44 : gy - 12 - rad - 10;   // keep the disc inside the short strip on the hourly page
    int arc = arc_max * (sp.night ? 8 : 10) / 10;
    int cy = gy - 12 - arc * sp.alt_1000 / 1000;
    if (sp.night) draw_moon(ctx, GPoint(cx, cy), rad, g_wx.moon_illum, g_wx.moon_phase <= 3, k->sky[1]);
    else          draw_sun(ctx, GPoint(cx, cy), rad, ph == PH_GOLDEN || ph == PH_DAWN);
  }

  draw_clouds(ctx, r, code, ph, k);
  draw_far_hills(ctx, r, gy + 2, k);
  draw_town(ctx, r, gy + 3, k, dark || (g >= 3), code == WX_SNOW, full ? 100 : 55);
  if (code == WX_FOG) draw_fog(ctx, r, ph);
  if (full) draw_tower(ctx, r.origin.x + W - 42, gy + 5, k);

  graphics_context_set_fill_color(ctx, k->near);   // ground and pines
  graphics_fill_rect(ctx, GRect(r.origin.x, gy + 3, W, r.origin.y + H - gy - 3), 0, GCornerNone);
  bool snow = (code == WX_SNOW);
  draw_pine(ctx, r.origin.x + W * 16 / 100, gy + 14, 34, k->tree, snow);
  draw_pine(ctx, r.origin.x + W * 27 / 100, gy + 16, 24, k->tree, snow);
  draw_pine(ctx, r.origin.x + W * 54 / 100, gy + 15, 28, k->tree, snow);

  GColor rain = dark ? GColorCeleste : (g >= 2 ? GColorWhite : GColorCeleste);
  GRect fall = GRect(r.origin.x, r.origin.y + 24, W, H - 24);
  switch (code) {
    case WX_DRIZZLE: draw_rain(ctx, fall, 0, rain); break;
    case WX_RAIN:    draw_rain(ctx, fall, 1, rain); break;
    case WX_HEAVY:   draw_rain(ctx, fall, 2, rain); break;
    case WX_THUNDER: draw_rain(ctx, fall, 2, rain); if (full) draw_bolt(ctx, r); break;
    case WX_SNOW:    draw_snow(ctx, GRect(r.origin.x, r.origin.y + 10, W, H - 10)); break;
    default: break;
  }
}

// ---- hero ------------------------------------------------------------------

static void draw_chip(GContext *ctx, GRect c, const char *cap, const char *val, Look *k, GColor vcol) {
  graphics_context_set_stroke_color(ctx, k->dim);
  graphics_draw_round_rect(ctx, c, 4);
  text_at(ctx, cap, s_font_tiny, GRect(c.origin.x, c.origin.y - 1, c.size.w, 16), k->dim, GTextAlignmentCenter);
  text_at(ctx, val, s_font_text, GRect(c.origin.x, c.origin.y + 11, c.size.w, 24), vcol, GTextAlignmentCenter);
}

static bool has_data(void) { return g_wx.updated != 0; }

// "No forecast yet": the quiet town under a plain day sky and a short, honest note.
// Shown on a fresh install until the phone answers, and again if it never does.
static void draw_nodata(GContext *ctx, GRect b, bool full) {
  int W = b.size.w, H = b.size.h;
  int sh = full ? H * SCENE_PCT / 100 : H;
  SkyPhase ph = data_phase_at(data_now());
  Look k = look_for(ph, WX_CLEAR);
  draw_scene(ctx, GRect(0, 0, W, sh), full, &k, ph, WX_CLEAR);
  if (!full) {
    text_outline(ctx, "No forecast yet", s_font_small, GRect(0, H / 2 - 12, W, 24), GColorWhite, GTextAlignmentCenter);
    return;
  }
  graphics_context_set_fill_color(ctx, k.panel);
  graphics_fill_rect(ctx, GRect(0, sh, W, H - sh), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_rect(ctx, GRect(0, sh, W, 4), 0, GCornerNone);
  text_at(ctx, "No forecast yet", s_font_text, GRect(6, sh + 8, W - 12, 26), k.ink, GTextAlignmentCenter);
  text_at(ctx, "Waiting for the phone. Press SELECT to try again.",
          s_font_tiny, GRect(10, sh + 34, W - 20, 52), k.dim, GTextAlignmentCenter);
  text_outline(ctx, g_wx.name, s_font_tiny, GRect(4, 1, W - 8, 18), GColorWhite, GTextAlignmentLeft);
}

// Age of the forecast as a short tag, or "" while it is fresh.
static void age_tag(char *buf, size_t n) {
  buf[0] = 0;
  if (data_is_mock() || !has_data()) return;
  int mins = (int)((time(NULL) - g_wx.updated) / 60);
  if (mins < STALE_AFTER_MIN) return;
  if (mins >= 48 * 60) snprintf(buf, n, "%dd ago", mins / 1440);
  else snprintf(buf, n, "%dh ago", mins / 60);
}

static void draw_hero(GContext *ctx, GRect b) {
  int W = b.size.w, H = b.size.h;
  if (!has_data()) { draw_nodata(ctx, b, true); return; }
  int sh = H * SCENE_PCT / 100;
  time_t now = data_now();
  SkyPhase ph = data_phase_at(now);
  Look k = look_for(ph, g_wx.code);
  draw_scene(ctx, GRect(0, 0, W, sh), true, &k, ph, g_wx.code);

  // the horizon rule in the temperature colour, then the panel
  graphics_context_set_fill_color(ctx, k.panel);
  graphics_fill_rect(ctx, GRect(0, sh, W, H - sh), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, temp_ink(g_wx.temp, g_wx.is_metric));
  graphics_fill_rect(ctx, GRect(0, sh, W, 4), 0, GCornerNone);

  bool light = gcolor_equal(k.ink, GColorBlack);
  char buf[24];
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", g_wx.temp);
  text_at(ctx, buf, s_font_temp, GRect(6, sh - 4, 110, 56), light ? k.ink : temp_ink(g_wx.temp, g_wx.is_metric), GTextAlignmentLeft);
  SunPos sp = data_sun_pos(now);
  text_at(ctx, wx_glyph(g_wx.code, sp.night), s_font_wx, GRect(W - 56, sh + 1, 50, 36), k.ink, GTextAlignmentRight);
  text_at(ctx, wx_summary(g_wx.code), s_font_tiny, GRect(92, sh + 33, W - 92 - 6, 18), k.ink, GTextAlignmentRight);

  int cy = H - 40, cw = (W - 8 * 2 - 2 * 4) / 3;
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", g_wx.tmax);
  draw_chip(ctx, GRect(8, cy, cw, 34), "HIGH", buf, &k, k.ink);
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", g_wx.tmin);
  draw_chip(ctx, GRect(8 + cw + 4, cy, cw, 34), "LOW", buf, &k, k.ink);
  if (g_wx.pop >= 20) {
    snprintf(buf, sizeof(buf), "%d%%", g_wx.pop);
    draw_chip(ctx, GRect(8 + 2 * (cw + 4), cy, cw, 34), "RAIN", buf, &k, light ? GColorBlueMoon : GColorCeleste);
  } else {
    snprintf(buf, sizeof(buf), "%d", g_wx.wind_speed);
    draw_chip(ctx, GRect(8 + 2 * (cw + 4), cy, cw, 34), g_wx.is_metric ? "KM/H" : "MPH", buf, &k, k.ink);
  }
  text_outline(ctx, g_wx.name, s_font_tiny, GRect(4, 1, W - 100, 18), GColorWhite, GTextAlignmentLeft);
#if SHOW_MOCK_TAG
  if (data_is_mock()) {
    text_outline(ctx, data_mock_name(), s_font_tiny, GRect(W - 98, 1, 94, 18), GColorWhite, GTextAlignmentRight);
  }
#endif
  char age[12];
  age_tag(age, sizeof(age));
  if (age[0]) text_outline(ctx, age, s_font_tiny, GRect(W - 98, 1, 94, 18), GColorWhite, GTextAlignmentRight);
}

// ---- hourly: a grid of little windows --------------------------------------

static void draw_hourly(GContext *ctx, GRect b) {
  int W = b.size.w, H = b.size.h;
  if (!has_data()) { draw_nodata(ctx, b, false); return; }
  time_t now = data_now();
  SkyPhase ph0 = data_phase_at(now);
  Look k0 = look_for(ph0, g_wx.code);
  int band = 72;
  draw_scene(ctx, GRect(0, 0, W, band), false, &k0, ph0, g_wx.code);
  graphics_context_set_fill_color(ctx, temp_ink(g_wx.temp, g_wx.is_metric));
  graphics_fill_rect(ctx, GRect(0, band, W, 3), 0, GCornerNone);

  int cols = 4, rows = 2;
  int cw = W / cols, ch = (H - band - 3) / rows;
  for (int i = 0; i < cols * rows; i++) {
    time_t t = now + (i + 1) * 3600;
    HourSlot *hs = &g_wx.hours[i];
    SkyPhase ph = data_phase_at(t);
    Look k = look_for(ph, hs->code);
    int x0 = (i % cols) * cw, y0 = band + 3 + (i / cols) * ch;
    int w = (i % cols == cols - 1) ? W - x0 : cw;
    GRect tile = GRect(x0 + 1, y0 + 1, w - 2, ch - 2);
    sky_paint(ctx, tile, k.sky, 4);
    int gh = 18;                               // little ground strip with the temperature on it
    graphics_context_set_fill_color(ctx, k.near);
    graphics_fill_rect(ctx, GRect(tile.origin.x, tile.origin.y + tile.size.h - gh, tile.size.w, gh), 0, GCornerNone);
    draw_pine(ctx, tile.origin.x + 8, tile.origin.y + tile.size.h - gh + 10, 18, k.tree, hs->code == WX_SNOW);
    char buf[12];
    snprintf(buf, sizeof(buf), "%d", localtime(&t)->tm_hour);
    text_outline(ctx, buf, s_font_tiny, GRect(tile.origin.x, tile.origin.y + 1, tile.size.w, 16), GColorWhite, GTextAlignmentCenter);
    text_outline(ctx, wx_glyph(hs->code, data_night_at(t)), s_font_wx_sm,
                 GRect(tile.origin.x, tile.origin.y + 17, tile.size.w, 26), GColorWhite, GTextAlignmentCenter);
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", hs->temp);
    text_at(ctx, buf, s_font_small, GRect(tile.origin.x, tile.origin.y + tile.size.h - 20, tile.size.w, 20),
            temp_ink(hs->temp, g_wx.is_metric), GTextAlignmentCenter);
    if (hs->pop >= 20) {                       // a raindrop bar on the left edge of the tile
      int bh = hs->pop * 28 / 100;
      graphics_context_set_fill_color(ctx, GColorCeleste);
      graphics_fill_rect(ctx, GRect(tile.origin.x + tile.size.w - 5, tile.origin.y + tile.size.h - gh - bh, 3, bh), 0, GCornerNone);
    }
  }
}

// ---- daily: the week as a skyline ------------------------------------------

static void draw_daily(GContext *ctx, GRect b) {
  int W = b.size.w, H = b.size.h;
  if (!has_data()) { draw_nodata(ctx, b, false); return; }
  time_t now = data_now();
  SkyPhase ph = data_phase_at(now);
  Look k = look_for(ph, g_wx.code);
  int base = H - 38;
  sky_paint(ctx, GRect(0, 0, W, base), k.sky, 4);
  if (ph == PH_NIGHT) draw_stars(ctx, GRect(0, 0, W, base), 12);
  draw_far_hills(ctx, GRect(0, 0, W, base), base, &k);
  graphics_context_set_fill_color(ctx, k.near);
  graphics_fill_rect(ctx, GRect(0, base, W, H - base), 0, GCornerNone);

  int lo = 127, hi = -128;
  for (int d = 0; d < DAYS_N; d++) {
    if (g_wx.days[d].max < lo) lo = g_wx.days[d].max;
    if (g_wx.days[d].max > hi) hi = g_wx.days[d].max;
  }
  if (hi - lo < 6) lo = hi - 6;
  int cw = W / DAYS_N;
  bool lights = (ph == PH_NIGHT || ph == PH_DUSK || sky_grade(g_wx.code) >= 3);
  GColor ink = gcolor_equal(k.near, GColorWhite) || gcolor_equal(k.near, GColorMayGreen) ? GColorBlack : GColorWhite;
  (void)ink;
  for (int d = 0; d < DAYS_N; d++) {
    DaySlot *ds = &g_wx.days[d];
    int x0 = d * cw, w = (d == DAYS_N - 1) ? W - x0 : cw;
    int bh = 24 + (ds->max - lo) * 56 / (hi - lo);     // building height follows the day's high
    int bx = x0 + 3, bw = w - 6, by = base - bh;
    graphics_context_set_fill_color(ctx, k.mid);
    graphics_fill_rect(ctx, GRect(bx, by, bw, bh), 0, GCornerNone);
    for (int wy = by + 7; wy + 3 < base - 3; wy += 7) {
      for (int wx = bx + 3; wx + 3 < bx + bw - 2; wx += 6) {
        bool lit = lights && (int)(hash2(wx, wy) % 100) < LIT_WINDOWS_PCT;
        graphics_context_set_fill_color(ctx, lit ? k.lit : k.unlit);
        graphics_fill_rect(ctx, GRect(wx, wy, 3, 3), 0, GCornerNone);
      }
    }
    graphics_context_set_fill_color(ctx, temp_ink(ds->max, g_wx.is_metric));   // roof in the ramp colour
    graphics_fill_rect(ctx, GRect(bx - 1, by - 3, bw + 2, 5), 1, GCornersAll);

    char buf[12];
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", ds->max);
    text_outline(ctx, buf, s_font_small, GRect(x0, 3, w, 20), temp_ink(ds->max, g_wx.is_metric), GTextAlignmentCenter);
    text_outline(ctx, wx_glyph(ds->code, false), s_font_wx_sm, GRect(x0, 22, w, 26), GColorWhite, GTextAlignmentCenter);
    if (d == 0) snprintf(buf, sizeof(buf), "Now");
    else strftime(buf, sizeof(buf), "%a", localtime(&(time_t){ now + d * 86400 }));
    text_at(ctx, buf, s_font_tiny, GRect(x0, base + 2, w, 16), GColorWhite, GTextAlignmentCenter);
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", ds->min);
    text_at(ctx, buf, s_font_tiny, GRect(x0, base + 17, w, 16), temp_ink(ds->min, g_wx.is_metric), GTextAlignmentCenter);
  }
}

// ---- canvas, paging, animation --------------------------------------------

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
#if DEBUG_LOG
  time_t s0; uint16_t m0; time_ms(&s0, &m0);
#endif
  switch (s_page) {
    case PAGE_HOURLY: draw_hourly(ctx, b); break;
    case PAGE_DAILY:  draw_daily(ctx, b);  break;
    default:          draw_hero(ctx, b);   break;
  }
#if DEBUG_LOG
  time_t s1; uint16_t m1; time_ms(&s1, &m1);
  APP_LOG(APP_LOG_LEVEL_INFO, "city: page %d drawn in %d ms, heap free %u", s_page,
          (int)((int64_t)(s1 - s0) * 1000 + m1 - m0), (unsigned)heap_bytes_free());
#endif
  int cx = b.size.w - 5;                       // page dots
  for (int i = 0; i < PAGE_N; i++) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(cx, 62 + i * 9), 3);
    graphics_context_set_fill_color(ctx, i == s_page ? GColorWhite : GColorDarkGray);
    graphics_fill_circle(ctx, GPoint(cx, 62 + i * 9), i == s_page ? 2 : 1);
  }
}

static void anim_cb(void *data);
static void anim_schedule(void) {
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
  if (ANIM_MS > 0 && s_anim_left > 0 && s_focused && s_page == PAGE_HERO && g_wx.code != WX_CLEAR && g_wx.updated != 0) {
    s_timer = app_timer_register(ANIM_MS, anim_cb, NULL);
  }
}
static void anim_cb(void *data) {
  s_timer = NULL;
  s_anim_left--;
  s_drift++;
  layer_mark_dirty(s_canvas);
  anim_schedule();
}

static void tap_handler(AccelAxisType axis, int32_t direction) {
  s_anim_left = SHAKE_TICKS;
  anim_schedule();
}

static void on_data(void) {
  if (s_canvas) layer_mark_dirty(s_canvas);
  anim_schedule();
}

static void page_step(int dir) {
  s_page = (s_page + dir + PAGE_N) % PAGE_N;
  layer_mark_dirty(s_canvas);
  anim_schedule();
}
static void up_click(ClickRecognizerRef r, void *c)   { page_step(-1); }
static void down_click(ClickRecognizerRef r, void *c) { page_step(1); }
static void select_click(ClickRecognizerRef r, void *c) {
  if (data_is_mock()) data_mock_cycle(1);
  else { data_request_refresh(); layer_mark_dirty(s_canvas); }
}
static void select_long(ClickRecognizerRef r, void *c) { data_mock_cycle(-1); }
static void click_config(void *c) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, select_long, NULL);
}

static void tick_handler(struct tm *t, TimeUnits units) {
  if (!data_is_mock()) layer_mark_dirty(s_canvas);
}
static void focus_handler(bool in_focus) { s_focused = in_focus; anim_schedule(); }

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}
static void window_unload(Window *window) { layer_destroy(s_canvas); s_canvas = NULL; }

static void init(void) {
  s_font_temp  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_AURA_48));
  s_font_text  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_AURA_20));
  s_font_small = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_AURA_16));
  s_font_tiny  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_AURA_14));
  s_font_wx    = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_WI_30));
  s_font_wx_sm = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_WI_20));

  PhaseCfg cfg = { DAWN_BEFORE_MIN, DAWN_AFTER_MIN, GOLDEN_BEFORE_MIN, DUSK_AFTER_MIN };
  data_init(MOCK, MOCK_SCENARIO, cfg, on_data);

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  app_focus_service_subscribe(focus_handler);
  accel_tap_service_subscribe(tap_handler);
  s_anim_left = SHAKE_TICKS;
  anim_schedule();
#if DEBUG_LOG
  APP_LOG(APP_LOG_LEVEL_INFO, "city: up, heap free %u used %u", (unsigned)heap_bytes_free(), (unsigned)heap_bytes_used());
#endif
}

static void deinit(void) {
  if (s_timer) app_timer_cancel(s_timer);
  accel_tap_service_unsubscribe();
  app_focus_service_unsubscribe();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
  data_deinit();
  fonts_unload_custom_font(s_font_temp);
  fonts_unload_custom_font(s_font_text);
  fonts_unload_custom_font(s_font_small);
  fonts_unload_custom_font(s_font_tiny);
  fonts_unload_custom_font(s_font_wx);
  fonts_unload_custom_font(s_font_wx_sm);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
