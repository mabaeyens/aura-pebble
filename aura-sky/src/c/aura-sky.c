#include <pebble.h>
#include "data.h"
#include "message_keys.auto.h"

// Aura Sky Weather: "living sky".
// A full-bleed procedural sky is the hero. It is painted from two inputs the phone
// already gives us: the time of day (night / dawn / day / golden / dusk, from real
// sunrise and sunset) and the condition (clear, cloud layers, fog, rain, snow,
// storm). A dark ground silhouette at the bottom carries a very small data overlay,
// so text is always light-on-dark no matter what the sky is doing.
// Pages: UP / DOWN. Hero, Hourly (the next hours as a strip of little skies),
// Daily (temperature ranges over a dimmed sky). SELECT refreshes (in mock mode it
// steps to the next scripted scenario instead).

// ---- knobs: the things to tweak first --------------------------------------
// MOCK, MOCK_SCENARIO and SHOW_MOCK_TAG can be set at build time without touching
// this file: AURA_SKY_MOCK=1 pebble build (see the wscript and the README).
#ifndef MOCK
#define MOCK                0    // 1 = scripted scenarios and a fake clock, no phone; 0 = real data
#endif
#ifndef MOCK_SCENARIO
#define MOCK_SCENARIO       0    // scenario shown at launch (0..7, see data.c)
#endif
#ifndef SHOW_MOCK_TAG
#define SHOW_MOCK_TAG       0    // print the scenario name top right while in mock mode
#endif
#define HORIZON_PCT        66    // where the sky meets the ground, % of screen height
#define DAWN_BEFORE_MIN    30    // dawn palette starts this long before sunrise...
#define DAWN_AFTER_MIN     60    // ...and ends this long after it
#define GOLDEN_BEFORE_MIN  75    // golden palette: the last N minutes before sunset
#define DUSK_AFTER_MIN     50    // dusk palette: N minutes after sunset
#define ANIM_MS          1000    // animation tick while the hero is animating; 0 = static
#define SHAKE_TICKS         8    // ticks of motion after a wrist shake (and at launch); 0 = never animate
#define CLOUD_DRIFT_PX      1    // clouds slide this many px per tick
#define RAIN_FALL_PX        5    // rain streaks fall this many px per tick
#define HOURLY_COLS         6    // hours shown in the hourly strip
#define DITHER_ROWS         4    // dither cell rows between sky colour bands (2px each)
#define SUN_RADIUS         13    // noon sun radius, px (the moon is a little smaller)
// -----------------------------------------------------------------------------

enum { PAGE_HERO = 0, PAGE_HOURLY, PAGE_DAILY, PAGE_N };

static Window   *s_window;
static Layer    *s_canvas;
static AppTimer *s_timer;
static int s_anim_left;   // ticks of motion left; a wrist shake refills it
static int       s_page = PAGE_HERO;
static int       s_drift;           // animation phase, advances once per tick
static bool      s_focused = true;
static bool      s_refreshing;      // a SELECT refresh is in flight
static AppTimer *s_refresh_timer;

static GFont s_font_temp;   // hero temperature (48)
static GFont s_font_text;   // labels (20)
static GFont s_font_small;  // captions (16)
static GFont s_font_tiny;   // dense captions (14)
static GFont s_font_wx;     // condition glyph, hero (30)
static GFont s_font_wx_sm;  // condition glyph, lists (20)

// ---- small drawing helpers -------------------------------------------------

static void text_at(GContext *ctx, const char *s, GFont f, GRect box,
                    GColor col, GTextAlignment align) {
  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, s, f, box, GTextOverflowModeTrailingEllipsis, align, NULL);
}

// Light text with a 1px dark drop shadow: stays readable on any sky colour.
static void text_shadow(GContext *ctx, const char *s, GFont f, GRect box,
                        GColor col, GTextAlignment align) {
  text_at(ctx, s, f, GRect(box.origin.x + 1, box.origin.y + 1, box.size.w, box.size.h),
          GColorBlack, align);
  text_at(ctx, s, f, box, col, align);
}

// Text with a 1px black outline: readable over any window colour.
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
  gpath_move_to(p, GPoint(-1, 0));   // gpath_draw_filled skips the left column
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

static int isqrt_i(int v) {
  if (v <= 0) return 0;
  int x = v, y = (x + 1) / 2;
  while (y < x) { x = y; y = (x + v / x) / 2; }
  return x;
}

// ---- sky palettes ----------------------------------------------------------
// Five stops, top to horizon, all on the 64-colour Emery palette. Painted as solid
// bands with an ordered dither between them, so there is no hard banding.

static int sky_grade(uint8_t code) {   // 0 clear, 1 muted, 2 grey, 3 dark, 4 fog
  if (code == WX_CLEAR || code == WX_FEW) return 0;
  if (code == WX_CLOUDY) return 1;
  if (code == WX_FOG) return 4;
  if (code == WX_HEAVY || code == WX_THUNDER) return 3;
  return 2;
}

static void sky_stops(SkyPhase ph, uint8_t code, GColor out[5]) {
  static const GColor CLEAR[5][5] = {
    /* night  */ { {GColorBlackARGB8}, {GColorOxfordBlueARGB8}, {GColorOxfordBlueARGB8},
                   {GColorDukeBlueARGB8}, {GColorIndigoARGB8} },
    /* dawn   */ { {GColorDukeBlueARGB8}, {GColorIndigoARGB8}, {GColorFashionMagentaARGB8},
                   {GColorMelonARGB8}, {GColorRajahARGB8} },
    /* day    */ { {GColorCobaltBlueARGB8}, {GColorVividCeruleanARGB8}, {GColorVividCeruleanARGB8},
                   {GColorPictonBlueARGB8}, {GColorCelesteARGB8} },
    /* golden */ { {GColorImperialPurpleARGB8}, {GColorIndigoARGB8}, {GColorJazzberryJamARGB8},
                   {GColorSunsetOrangeARGB8}, {GColorRajahARGB8} },
    /* dusk   */ { {GColorBlackARGB8}, {GColorOxfordBlueARGB8}, {GColorIndigoARGB8},
                   {GColorJazzberryJamARGB8}, {GColorSunsetOrangeARGB8} },
  };
  static const GColor GREY[5][5] = {
    /* night  */ { {GColorBlackARGB8}, {GColorBlackARGB8}, {GColorBlackARGB8},
                   {GColorOxfordBlueARGB8}, {GColorDarkGrayARGB8} },
    /* dawn   */ { {GColorDarkGrayARGB8}, {GColorDarkGrayARGB8}, {GColorDarkGrayARGB8},
                   {GColorLightGrayARGB8}, {GColorMelonARGB8} },
    /* day    */ { {GColorDarkGrayARGB8}, {GColorDarkGrayARGB8}, {GColorCadetBlueARGB8},
                   {GColorCadetBlueARGB8}, {GColorLightGrayARGB8} },
    /* golden */ { {GColorDarkGrayARGB8}, {GColorDarkGrayARGB8}, {GColorDarkGrayARGB8},
                   {GColorLightGrayARGB8}, {GColorRajahARGB8} },
    /* dusk   */ { {GColorBlackARGB8}, {GColorBlackARGB8}, {GColorDarkGrayARGB8},
                   {GColorDarkGrayARGB8}, {GColorImperialPurpleARGB8} },
  };
  int g = sky_grade(code);
  for (int i = 0; i < 5; i++) {
    if (g == 0)      out[i] = CLEAR[ph][i];
    else if (g == 1) out[i] = (i < 2) ? CLEAR[ph][i] : GREY[ph][i];
    else             out[i] = GREY[ph][i];
  }
  if (g == 3) {                      // storm: crush everything toward black
    out[0] = GColorBlack; out[1] = GColorBlack;
    if (ph != PH_NIGHT) { out[2] = GColorDarkGray; out[3] = GColorDarkGray; }
    out[4] = (ph == PH_NIGHT) ? GColorDarkGray : GColorCadetBlue;
  }
  if (g == 4) {                      // fog: bright, flat, low contrast
    if (ph == PH_NIGHT || ph == PH_DUSK) {
      out[0] = GColorBlack; out[1] = GColorDarkGray; out[2] = GColorDarkGray;
      out[3] = GColorDarkGray; out[4] = GColorLightGray;
    } else {
      out[0] = GColorDarkGray; out[1] = GColorCadetBlue; out[2] = GColorLightGray;
      out[3] = GColorLightGray; out[4] = GColorWhite;
    }
  }
}

static const uint8_t BAYER[4][4] = {
  { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
};

// Paint `n` of the stops stretched over `r`, with ordered-dither transitions.
static void sky_paint(GContext *ctx, GRect r, const GColor *st, int n) {
  int bh = r.size.h / n;
  for (int i = 0; i < n; i++) {
    int y0 = r.origin.y + i * bh;
    int h = (i == n - 1) ? (r.origin.y + r.size.h - y0) : bh;
    graphics_context_set_fill_color(ctx, st[i]);
    graphics_fill_rect(ctx, GRect(r.origin.x, y0, r.size.w, h), 0, GCornerNone);
  }
  int rows = DITHER_ROWS;                      // seam = rows*2 dither cell rows, 2px each
  for (int i = 1; i < n; i++) {
    int top = r.origin.y + i * bh - rows * 2;  // centred on the band edge
    graphics_context_set_fill_color(ctx, st[i - 1]);
    graphics_fill_rect(ctx, GRect(r.origin.x, top, r.size.w, rows * 4), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, st[i]);
    for (int cy = 0; cy < rows * 2; cy++) {
      int level = (cy * 16 + 8) / (rows * 2);  // density of the lower colour, 0..16
      for (int cx = 0; cx < r.size.w / 2; cx++) {
        if (BAYER[cy & 3][cx & 3] < level) {
          graphics_fill_rect(ctx, GRect(r.origin.x + cx * 2, top + cy * 2, 2, 2), 0, GCornerNone);
        }
      }
    }
  }
}

// ---- sun, moon, stars, clouds, precipitation -------------------------------

static void draw_glow(GContext *ctx, GPoint c, int r, GColor col) {
  graphics_context_set_stroke_color(ctx, col);
  for (int dy = -r; dy <= r; dy++) {
    int half = isqrt_i(r * r - dy * dy);
    for (int dx = -half; dx <= half; dx++) {
      if (((c.x + dx + c.y + dy) & 1) == 0) graphics_draw_pixel(ctx, GPoint(c.x + dx, c.y + dy));
    }
  }
}

static void draw_sun(GContext *ctx, GPoint c, int r, bool low) {
  draw_glow(ctx, c, r + 8, low ? GColorMelon : GColorCeleste);
  graphics_context_set_fill_color(ctx, GColorOrange);
  graphics_fill_circle(ctx, c, r);
  graphics_context_set_fill_color(ctx, low ? GColorChromeYellow : GColorYellow);
  graphics_fill_circle(ctx, c, r - 2);
}

static void draw_moon(GContext *ctx, GPoint c, int r, int lit_pct, bool waxing, GColor sky) {
  draw_glow(ctx, c, r + 5, GColorDukeBlue);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, c, r);
  if (lit_pct < 97) {
    int dx = lit_pct * 2 * r / 100;           // slide a sky-coloured disc off the moon
    graphics_context_set_fill_color(ctx, sky);
    graphics_fill_circle(ctx, GPoint(c.x + (waxing ? -dx : dx), c.y), r);
  }
}

static const uint8_t STAR_X[] = { 10, 31, 52, 74, 91, 22, 63, 85, 40, 97, 5, 70, 47, 15 };
static const uint8_t STAR_Y[] = { 12, 8, 20, 10, 24, 34, 38, 46, 52, 60, 56, 70, 84, 80 };

static void draw_stars(GContext *ctx, GRect sky, int count) {
  for (int i = 0; i < count && i < (int)sizeof(STAR_X); i++) {
    int x = sky.origin.x + sky.size.w * STAR_X[i] / 100;
    int y = sky.origin.y + sky.size.h * STAR_Y[i] / 100 * 85 / 100;
    graphics_context_set_fill_color(ctx, (i % 3 == 0) ? GColorCeleste : GColorWhite);
    int s = (i % 4 == 0) ? 2 : 1;
    graphics_fill_rect(ctx, GRect(x, y, s, s), 0, GCornerNone);
  }
}

// One pixel-art puff: a rounded base and two bumps, with a darker underside.
static void draw_cloud(GContext *ctx, int x, int y, int w, GColor main, GColor shade) {
  int h = w * 42 / 100;
  for (int pass = 0; pass < 2; pass++) {
    int oy = pass == 0 ? 2 : 0;
    graphics_context_set_fill_color(ctx, pass == 0 ? shade : main);
    graphics_fill_rect(ctx, GRect(x, y + h / 2 + oy, w, h / 2), h / 4, GCornersAll);
    graphics_fill_circle(ctx, GPoint(x + w * 33 / 100, y + h * 40 / 100 + oy), h * 40 / 100);
    graphics_fill_circle(ctx, GPoint(x + w * 62 / 100, y + h * 32 / 100 + oy), h * 50 / 100);
  }
}

typedef struct { GColor main, shade; } CloudCol;

static CloudCol cloud_colors(SkyPhase ph, int grade) {
  CloudCol c;
  if (grade >= 3) { c.main = GColorDarkGray; c.shade = GColorBlack; return c; }
  switch (ph) {
    case PH_NIGHT:  c.main = GColorDarkGray;  c.shade = GColorBlack; break;
    case PH_DAWN:   c.main = GColorMelon;     c.shade = GColorFashionMagenta; break;
    case PH_GOLDEN: c.main = GColorMelon;     c.shade = GColorSunsetOrange; break;
    case PH_DUSK:   c.main = GColorDarkGray;  c.shade = GColorBlack; break;
    default:        c.main = GColorWhite;     c.shade = GColorCeleste; break;
  }
  if (grade == 2 || grade == 4) {            // grey weather: dull the clouds
    if (ph == PH_NIGHT || ph == PH_DUSK) { c.main = GColorDarkGray; c.shade = GColorBlack; }
    else { c.main = GColorLightGray; c.shade = GColorDarkGray; }
  }
  return c;
}

// Clouds scroll left to right and wrap. `span` is the wrap width.
static int drift_x(int base, int w, int W, int speed) {
  int span = W + w + 20;
  int x = (base + s_drift * CLOUD_DRIFT_PX * speed) % span;
  return x - w - 10;
}

// A cloud deck: a solid body with a bumpy lower edge that scrolls sideways. The bump
// pattern is hashed from the world column so it scrolls without popping.
static void draw_deck(GContext *ctx, int W, int ybase, GColor main, GColor shade, int seed) {
  int scroll = s_drift * CLOUD_DRIFT_PX * seed;
  for (int pass = 0; pass < 2; pass++) {
    int oy = (pass == 0) ? 3 : 0;
    graphics_context_set_fill_color(ctx, pass == 0 ? shade : main);
    graphics_fill_rect(ctx, GRect(0, 0, W, ybase - 8 + oy), 0, GCornerNone);
    for (int i = -2; i <= W / 14 + 2; i++) {
      int idx = i - scroll / 14 + seed * 7;
      uint32_t h = (uint32_t)(idx * 2654435761u);
      h ^= h >> 15;
      int r = 9 + (int)(h % 8);
      int cy = ybase - 10 + (int)((h >> 5) % 8);
      graphics_fill_circle(ctx, GPoint(i * 14 + scroll % 14, cy + oy), r);
    }
  }
}

static void draw_cloud_layer(GContext *ctx, GRect sky, uint8_t code, SkyPhase ph) {
  int W = sky.size.w, H = sky.size.h;
  int grade = sky_grade(code);
  CloudCol cc = cloud_colors(ph, grade);
  switch (code) {
    case WX_FEW:
      draw_cloud(ctx, drift_x(W * 20 / 100, 56, W, 1), H * 30 / 100, 56, cc.main, cc.shade);
      draw_cloud(ctx, drift_x(W * 75 / 100, 40, W, 2), H * 52 / 100, 40, cc.main, cc.shade);
      break;
    case WX_CLOUDY:
      draw_cloud(ctx, drift_x(W * 10 / 100, 64, W, 1), H * 24 / 100, 64, cc.main, cc.shade);
      draw_cloud(ctx, drift_x(W * 55 / 100, 48, W, 2), H * 40 / 100, 48, cc.main, cc.shade);
      draw_cloud(ctx, drift_x(W * 90 / 100, 70, W, 1), H * 56 / 100, 70, cc.main, cc.shade);
      draw_cloud(ctx, drift_x(W * 35 / 100, 36, W, 3), H * 12 / 100, 36, cc.main, cc.shade);
      break;
    case WX_FOG:
      break;                                   // fog is dither bands, drawn separately
    case WX_CLEAR:
      break;
    default: {                                 // overcast and wetter: a thick deck
      int base = (code == WX_OVERCAST) ? 34 : (grade == 3 ? 46 : 40);   // % of sky height
      if (code == WX_SNOW && ph != PH_NIGHT && ph != PH_DUSK) cc.main = GColorCadetBlue;   // flakes need contrast
      bool murky = (ph == PH_NIGHT || ph == PH_DUSK || grade >= 3);
      GColor back = murky ? GColorDarkGray : cc.shade;   // two layers of the same grey read as depth
      draw_deck(ctx, W, H * (base + 7) / 100, back, back, 1);
      draw_deck(ctx, W, H * base / 100, cc.main, cc.shade, 2);
      break;
    }
  }
}

static void draw_fog(GContext *ctx, GRect sky, SkyPhase ph) {
  GColor col = (ph == PH_NIGHT || ph == PH_DUSK) ? GColorLightGray : GColorWhite;
  graphics_context_set_fill_color(ctx, col);
  int H = sky.size.h, W = sky.size.w;
  int y0 = H * 38 / 100, span = H - y0;
  int off = (s_drift & 7);
  for (int y = y0; y < H; y += 2) {            // density rises smoothly toward the ground
    int dens = (y - y0) * 15 / span;
    for (int x = -8; x < W; x += 2) {
      int cx = (x + off) >> 1;
      if (BAYER[(y >> 1) & 3][cx & 3] < dens) {
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
      int h = x * 31 + y * 17;
      if (h < 0) h = -h;
      int px = x + (h % stepx), py = y + (h % 15) + fall;
      if (py < r.origin.y || py + len > r.origin.y + r.size.h) continue;   // stay in the sky
      graphics_draw_line(ctx, GPoint(px, py), GPoint(px - 3, py + len));
    }
  }
}

static void draw_snow(GContext *ctx, GRect r) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  int step = 18;
  int fall = (s_drift * 2) % step;
  for (int gx = r.origin.x - step; gx < r.origin.x + r.size.w + step; gx += step) {
    for (int gy = r.origin.y - step; gy < r.origin.y + r.size.h; gy += step) {
      uint32_t hh = (uint32_t)gx * 374761393u + (uint32_t)gy * 668265263u;
      hh = (hh ^ (hh >> 13)) * 1274126177u;
      hh ^= hh >> 16;
      if ((hh & 7u) == 0u) continue;
      int jx = (int)(hh % (uint32_t)step) + ((s_drift & 3) - 1) * ((hh >> 5) & 1);
      int jy = (int)((hh >> 8) % (uint32_t)step) + fall;
      int rad = (((hh >> 17) & 3u) == 0u) ? 1 : ((((hh >> 19) & 7u) == 0u) ? 3 : 2);
      int py = gy + jy;
      if (py < r.origin.y + 2 || py > r.origin.y + r.size.h - 2) continue;
      graphics_fill_circle(ctx, GPoint(gx + jx, py), rad);
    }
  }
}

static void draw_bolt(GContext *ctx, GRect sky) {
  int bx = sky.origin.x + sky.size.w * 70 / 100;
  int by = sky.origin.y + sky.size.h * 30 / 100;
  GPoint bolt[] = {
    {bx, by}, {bx - 9, by + 22}, {bx - 1, by + 22},
    {bx - 11, by + 46}, {bx + 8, by + 16}, {bx, by + 16}, {bx + 8, by},
  };
  fill_poly(ctx, bolt, sizeof(bolt) / sizeof(bolt[0]), GColorYellow);
}

// ---- ground ----------------------------------------------------------------

static void ground_colors(SkyPhase ph, uint8_t code, GColor *back, GColor *front) {
  int g = sky_grade(code);
  switch (ph) {
    case PH_DAWN:   *back = GColorArmyGreen;      *front = GColorDarkGreen; break;
    case PH_GOLDEN: *back = GColorWindsorTan;     *front = GColorImperialPurple; break;
    case PH_DUSK:   *back = GColorImperialPurple; *front = GColorBlack; break;
    case PH_NIGHT:  *back = GColorOxfordBlue;     *front = GColorBlack; break;
    default:        *back = GColorKellyGreen;     *front = GColorDarkGreen; break;
  }
  if (g >= 2 && ph != PH_NIGHT) { *back = GColorDarkGray; *front = GColorBlack; }
  if (g == 3) { *back = GColorDarkGray; *front = GColorBlack; }
  if (code == WX_SNOW) { *back = ph == PH_NIGHT ? GColorLightGray : GColorWhite; *front = GColorDarkGray; }
}

static void draw_ground(GContext *ctx, GRect b, int hy, uint8_t code, SkyPhase ph) {
  GColor back, front;
  ground_colors(ph, code, &back, &front);
  int W = b.size.w, H = b.size.h;
  GPoint bk[] = {
    {0, hy - 6}, {W * 18 / 100, hy - 16}, {W * 38 / 100, hy - 8}, {W * 60 / 100, hy - 19},
    {W * 80 / 100, hy - 9}, {W, hy - 14}, {W, H}, {0, H},
  };
  fill_poly(ctx, bk, sizeof(bk) / sizeof(bk[0]), back);
  GPoint fr[] = {
    {0, hy + 3}, {W * 25 / 100, hy - 3}, {W * 55 / 100, hy + 4}, {W * 80 / 100, hy - 2},
    {W, hy + 3}, {W, H}, {0, H},
  };
  fill_poly(ctx, fr, sizeof(fr) / sizeof(fr[0]), front);
}

// ---- hero ------------------------------------------------------------------

static void draw_hero(GContext *ctx, GRect b) {
  int W = b.size.w, H = b.size.h;
  int hy = H * HORIZON_PCT / 100;
  time_t now = data_now();
  SkyPhase ph = data_phase_at(now);
  SunPos sp = data_sun_pos(now);
  uint8_t code = g_wx.code;
  int grade = sky_grade(code);

  GColor st[5];
  sky_stops(ph, code, st);
  GRect sky = GRect(0, 0, W, hy + 10);
  sky_paint(ctx, sky, st, 5);

  if (sp.night && grade <= 1) draw_stars(ctx, sky, grade == 0 ? 14 : 6);
  else if (ph == PH_DUSK && grade == 0) draw_stars(ctx, sky, 5);

  // The disc: hidden behind a thick deck, drawn behind thin clouds.
  bool covered = (grade >= 2);
  if (!covered) {
    int r = (sp.night ? SUN_RADIUS - 2 : SUN_RADIUS) * (620 + 380 * sp.alt_1000 / 1000) / 1000;
    int cx = r + 4 + (W - 2 * (r + 4)) * sp.frac_1000 / 1000;
    int arc = sp.night ? (hy - 56) * 8 / 10 : (hy - 40);
    int cy = hy - 6 - arc * sp.alt_1000 / 1000;
    if (sp.night) draw_moon(ctx, GPoint(cx, cy), r, g_wx.moon_illum, g_wx.moon_phase <= 3, st[1]);
    else          draw_sun(ctx, GPoint(cx, cy), r, ph == PH_GOLDEN || ph == PH_DAWN);
  }

  GRect clouds = GRect(0, 6, W, hy - 6);
  draw_cloud_layer(ctx, clouds, code, ph);
  if (code == WX_FOG) draw_fog(ctx, sky, ph);

  bool night_ish = (ph == PH_NIGHT || ph == PH_DUSK);
  GColor rain = night_ish ? GColorCeleste : (grade >= 2 ? GColorWhite : GColorCeleste);
  GRect fall = GRect(0, 40, W, hy - 40 + 4);
  switch (code) {
    case WX_DRIZZLE: draw_rain(ctx, fall, 0, rain); break;
    case WX_RAIN:    draw_rain(ctx, fall, 1, rain); break;
    case WX_HEAVY:   draw_rain(ctx, fall, 2, rain); break;
    case WX_THUNDER: draw_rain(ctx, fall, 2, rain); draw_bolt(ctx, sky); break;
    case WX_SNOW:    draw_snow(ctx, GRect(0, 30, W, hy - 30 + 4)); break;
    default: break;
  }

  draw_ground(ctx, b, hy, code, ph);

  // --- overlay ---
  // The header sits straight on the sky, which can be near white (fog, snow,
  // overcast days), so it gets a full 1px black outline, not a drop shadow.
  text_outline(ctx, data_has_forecast() ? g_wx.name : "Aura Sky", s_font_small,
               GRect(8, 0, W - 80, 20), GColorWhite, GTextAlignmentLeft);
  const char *note = NULL;
  char nbuf[16];
#if SHOW_MOCK_TAG
  if (data_is_mock()) note = data_mock_name();
#endif
  if (!note && s_refreshing) note = "Updating";
  if (!note && data_has_forecast()) {
    int age = data_age_min();
    if (age >= 1440)     { snprintf(nbuf, sizeof(nbuf), "%dd old", age / 1440); note = nbuf; }
    else if (age >= 180) { snprintf(nbuf, sizeof(nbuf), "%dh old", age / 60);   note = nbuf; }
  }
  if (note) {
    text_outline(ctx, note, s_font_tiny, GRect(W - 98, 2, 94, 18), GColorWhite, GTextAlignmentRight);
  }

  if (!data_has_forecast()) {
    text_shadow(ctx, "No forecast yet", s_font_text, GRect(8, hy + 2, W - 16, 26), GColorWhite, GTextAlignmentLeft);
    text_shadow(ctx, data_fetch_failed() ? "Could not fetch it." : "Waiting for phone.",
                s_font_small, GRect(8, hy + 26, W - 16, 20), GColorLightGray, GTextAlignmentLeft);
    text_shadow(ctx, "Check the phone link.", s_font_tiny, GRect(8, hy + 46, W - 16, 18), GColorLightGray, GTextAlignmentLeft);
    text_shadow(ctx, "SELECT to retry.", s_font_tiny, GRect(8, hy + 60, W - 16, 18), GColorLightGray, GTextAlignmentLeft);
    return;
  }

  char buf[24];
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", g_wx.temp);
  text_shadow(ctx, buf, s_font_temp, GRect(6, hy - 2, 110, 56), temp_ink(g_wx.temp, g_wx.is_metric), GTextAlignmentLeft);

  text_shadow(ctx, wx_glyph(code, sp.night), s_font_wx, GRect(94, hy + 8, 54, 36), GColorWhite, GTextAlignmentCenter);

  snprintf(buf, sizeof(buf), "%d\xC2\xB0", g_wx.tmax);
  text_shadow(ctx, buf, s_font_small, GRect(W - 56, hy + 6, 50, 20), temp_ink(g_wx.tmax, g_wx.is_metric), GTextAlignmentRight);
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", g_wx.tmin);
  text_shadow(ctx, buf, s_font_small, GRect(W - 56, hy + 26, 50, 20), temp_ink(g_wx.tmin, g_wx.is_metric), GTextAlignmentRight);

  text_shadow(ctx, wx_summary(code), s_font_text, GRect(8, H - 30, 120, 26), GColorWhite, GTextAlignmentLeft);
  if (g_wx.pop >= 20) {                        // the most useful second fact: rain if likely, else wind
    snprintf(buf, sizeof(buf), "%d%% rain", g_wx.pop);
    text_shadow(ctx, buf, s_font_small, GRect(W - 104, H - 26, 98, 22), GColorCeleste, GTextAlignmentRight);
  } else {
    snprintf(buf, sizeof(buf), "%d %s", g_wx.wind_speed, g_wx.is_metric ? "km/h" : "mph");
    text_shadow(ctx, buf, s_font_small, GRect(W - 104, H - 26, 98, 22), GColorLightGray, GTextAlignmentRight);
  }
}

// ---- hourly: a strip of little skies ---------------------------------------
// Each hour is a window onto the sky as it will be at that hour (its own colour
// phase and condition), with the temperature trend drawn across all of them.

static void draw_empty(GContext *ctx, GRect b, const char *what) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  text_at(ctx, "No forecast yet", s_font_text, GRect(0, b.size.h / 2 - 30, b.size.w, 26), GColorWhite, GTextAlignmentCenter);
  text_at(ctx, what, s_font_small, GRect(10, b.size.h / 2 + 2, b.size.w - 20, 40), GColorLightGray, GTextAlignmentCenter);
}

static void draw_hourly(GContext *ctx, GRect b) {
  if (!data_has_forecast()) { draw_empty(ctx, b, "The next hours appear once the phone has answered."); return; }
  int W = b.size.w;
  int n = HOURLY_COLS;
  int cw = W / n;
  time_t now = data_now();
  int tmin = 127, tmax = -128;
  for (int i = 0; i < n; i++) {
    if (g_wx.hours[i].temp < tmin) tmin = g_wx.hours[i].temp;
    if (g_wx.hours[i].temp > tmax) tmax = g_wx.hours[i].temp;
  }
  if (tmax - tmin < 4) tmax = tmin + 4;
  int wy0 = 50, wy1 = 178;                     // the sky windows
  int ytop = wy0 + 34, ybot = wy1 - 16;
  int pts_x[HOURLY_COLS], pts_y[HOURLY_COLS];
  for (int i = 0; i < n; i++) {
    time_t t = now + (i + 1) * 3600;
    HourSlot *hs = &g_wx.hours[i];
    GColor st[5];
    sky_stops(data_phase_at(t), hs->code, st);
    int x0 = i * cw;
    int w = (i == n - 1) ? W - x0 : cw;
    sky_paint(ctx, GRect(x0 + 1, wy0, w - 2, wy1 - wy0), st, 5);
    pts_x[i] = x0 + w / 2;
    pts_y[i] = ybot - (hs->temp - tmin) * (ybot - ytop) / (tmax - tmin);

    char buf[12];
    snprintf(buf, sizeof(buf), "%d", localtime(&t)->tm_hour);
    text_at(ctx, buf, s_font_small, GRect(x0, 2, w, 20), GColorWhite, GTextAlignmentCenter);
    text_at(ctx, wx_glyph(hs->code, data_night_at(t)), s_font_wx_sm, GRect(x0, 22, w, 26),
            GColorWhite, GTextAlignmentCenter);
  }
  for (int i = 1; i < n; i++) {                // trend line: black casing, white core
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 4);
    graphics_draw_line(ctx, GPoint(pts_x[i - 1], pts_y[i - 1]), GPoint(pts_x[i], pts_y[i]));
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, GPoint(pts_x[i - 1], pts_y[i - 1]), GPoint(pts_x[i], pts_y[i]));
  }
  graphics_context_set_stroke_width(ctx, 1);
  for (int i = 0; i < n; i++) {
    HourSlot *hs = &g_wx.hours[i];
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(pts_x[i], pts_y[i]), 5);
    graphics_context_set_fill_color(ctx, temp_ink(hs->temp, g_wx.is_metric));
    graphics_fill_circle(ctx, GPoint(pts_x[i], pts_y[i]), 4);
    char buf[12];
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", hs->temp);
    text_outline(ctx, buf, s_font_tiny, GRect(pts_x[i] - cw / 2, pts_y[i] - 25, cw, 18),
                 GColorWhite, GTextAlignmentCenter);

    if (hs->pop >= 10) {                       // rain chance: a bar growing up from the base
      int bh = hs->pop * 26 / 100;
      graphics_context_set_fill_color(ctx, GColorCeleste);
      graphics_fill_rect(ctx, GRect(pts_x[i] - 6, 206 - bh, 12, bh), 2, GCornersTop);
      snprintf(buf, sizeof(buf), "%d", hs->pop);
      text_at(ctx, buf, s_font_tiny, GRect(pts_x[i] - cw / 2, 206, cw, 18), GColorCeleste, GTextAlignmentCenter);
    }
  }
}

// ---- daily: ranges over a dimmed sky ---------------------------------------

static void draw_daily(GContext *ctx, GRect b) {
  if (!data_has_forecast()) { draw_empty(ctx, b, "The next days appear once the phone has answered."); return; }
  int H = b.size.h;
  time_t now = data_now();
  SkyPhase ph = data_phase_at(now);
  GColor st[5];
  sky_stops(ph, g_wx.code, st);
  sky_paint(ctx, b, st, 3);

  int lo = 127, hi = -128;
  for (int d = 0; d < DAYS_N; d++) {
    if (g_wx.days[d].min < lo) lo = g_wx.days[d].min;
    if (g_wx.days[d].max > hi) hi = g_wx.days[d].max;
  }
  if (hi - lo < 6) hi = lo + 6;
  int rowh = H / DAYS_N;
  int bx = 104, bw = 56;
  for (int d = 0; d < DAYS_N; d++) {
    DaySlot *ds = &g_wx.days[d];
    int y = d * rowh;
    char buf[16];
    if (d == 0) snprintf(buf, sizeof(buf), "Today");
    else strftime(buf, sizeof(buf), "%a", localtime(&(time_t){ now + d * 86400 }));
    text_outline(ctx, buf, d == 0 ? s_font_tiny : s_font_small, GRect(6, y + rowh / 2 - 12, 56, 22),
                GColorWhite, GTextAlignmentLeft);
    text_outline(ctx, wx_glyph(ds->code, false), s_font_wx_sm, GRect(50, y + rowh / 2 - 14, 34, 26),
                GColorWhite, GTextAlignmentCenter);
    snprintf(buf, sizeof(buf), "%d", ds->min);
    text_outline(ctx, buf, s_font_small, GRect(76, y + rowh / 2 - 12, 26, 22), GColorLightGray, GTextAlignmentRight);
    // range bar: ramp-coloured, one line per x column
    int x0 = bx + (ds->min - lo) * bw / (hi - lo);
    int x1 = bx + (ds->max - lo) * bw / (hi - lo);
    if (x1 < x0 + 3) x1 = x0 + 3;
    int by = y + rowh / 2 - 3;
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(bx - 1, by - 1, bw + 2, 8), 3, GCornersAll);
    for (int x = x0; x <= x1; x++) {
      int t = ds->min + (x - x0) * (ds->max - ds->min) / (x1 - x0);
      graphics_context_set_stroke_color(ctx, temp_ink(t, g_wx.is_metric));
      graphics_draw_line(ctx, GPoint(x, by), GPoint(x, by + 5));
    }
    snprintf(buf, sizeof(buf), "%d", ds->max);
    text_outline(ctx, buf, s_font_small, GRect(bx + bw + 3, y + rowh / 2 - 12, 30, 22),
                temp_ink(ds->max, g_wx.is_metric), GTextAlignmentLeft);
  }
}

// ---- canvas, paging, animation --------------------------------------------

static void draw_dots(GContext *ctx, GRect b) {
  int cx = b.size.w - 5, cy = 60;
  for (int i = 0; i < PAGE_N; i++) {
    graphics_context_set_fill_color(ctx, i == s_page ? GColorWhite : GColorDarkGray);
    graphics_fill_circle(ctx, GPoint(cx, cy + i * 9), i == s_page ? 3 : 2);
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  switch (s_page) {
    case PAGE_HOURLY: draw_hourly(ctx, b); break;
    case PAGE_DAILY:  draw_daily(ctx, b);  break;
    default:          draw_hero(ctx, b);   break;
  }
  if (s_page != PAGE_HOURLY) draw_dots(ctx, b);
}

static void anim_cb(void *data);
static void anim_schedule(void) {
  if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
  if (ANIM_MS > 0 && s_anim_left > 0 && s_focused && s_page == PAGE_HERO && g_wx.code != WX_CLEAR) {
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
  s_refreshing = false;
  if (s_canvas) layer_mark_dirty(s_canvas);
  anim_schedule();
}

static void page_step(int dir) {
  s_page = (s_page + dir + PAGE_N) % PAGE_N;
  layer_mark_dirty(s_canvas);
  anim_schedule();
}
static void refresh_timeout(void *data) {
  s_refresh_timer = NULL;
  s_refreshing = false;
  if (s_canvas) layer_mark_dirty(s_canvas);
}
static void up_click(ClickRecognizerRef r, void *c)   { page_step(-1); }
static void down_click(ClickRecognizerRef r, void *c) { page_step(1); }
static void select_click(ClickRecognizerRef r, void *c) {
  if (data_is_mock()) data_mock_cycle(1);
  else {
    data_request_refresh();
    s_refreshing = true;
    if (s_refresh_timer) app_timer_cancel(s_refresh_timer);
    s_refresh_timer = app_timer_register(10000, refresh_timeout, NULL);
    layer_mark_dirty(s_canvas);
  }
}
static void select_long(ClickRecognizerRef r, void *c) { data_mock_cycle(-1); }
static void click_config(void *c) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
  window_long_click_subscribe(BUTTON_ID_SELECT, 0, select_long, NULL);
}

static void tick_handler(struct tm *t, TimeUnits units) {
  if (!data_is_mock()) layer_mark_dirty(s_canvas);   // the sun moves, once a minute
}

static void focus_handler(bool in_focus) {
  s_focused = in_focus;
  anim_schedule();
}

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
}

static void deinit(void) {
  if (s_timer) app_timer_cancel(s_timer);
  if (s_refresh_timer) app_timer_cancel(s_refresh_timer);
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
