#include <pebble.h>
#include "weather.h"
#include "sky.h"
#include "message_keys.auto.h"

// Aura Weather - a standalone weather watchapp for the Pebble Time 2.
// Step 2: the live Open-Meteo bridge. The phone (src/pkjs) fetches, normalises
// and ships the forecast in framed AppMessages; this side fills the struct,
// persists it, and renders. SELECT requests a refresh. UP/DOWN page the three
// screens; the hero (step 1) draws the procedural sun over the sky.

// The card stack (docs/06). Order is the natural default; cards appear and
// disappear by data availability (aviso only with an active warning, UV only
// when the sun reaches a peak), so navigation walks the *visible* cards.
enum {
  CARD_HERO = 0,
  CARD_AVISO,
  CARD_HOURLY,
  CARD_DAILY,
  CARD_SUNMOON,
  CARD_WIND,
  CARD_UV,
  CARD_DETAILS,
  CARD_BULLETIN,
  CARD_N
};

#define PKEY_WEATHER  1   // persist slot for the whole Weather struct
#define PKEY_BULLETIN 2   // the bulletin text (kept out of the struct: persist caps at 256 B)

// The generated / official forecast prose. Lives outside the persisted Weather
// struct because that struct plus a ~256 B string would exceed persist's 256 B
// per-key limit; it gets its own key and its own frame.
static char s_bulletin[256];

static Window    *s_window;
static Layer     *s_canvas;
static int        s_card = CARD_HERO;

static GFont s_font_xl;     // extra-large hero temperature
static GFont s_font_big;    // large current temperature
static GFont s_font_text;   // labels, hi/lo, location, weekday
static GFont s_font_small;  // staleness note
static GFont s_font_wx;     // condition glyph, hero size
static GFont s_font_wx_sm;  // condition glyph, list size

static Weather s_wx;        // the single in-memory forecast (from the phone / persist)

static void request_refresh(void);

// ---- colour + glyph helpers (weather.h) ------------------------------------

GColor temp_color(int temp, bool is_metric) {
  int c = is_metric ? temp : (temp - 32) * 5 / 9;   // tint by real feel, not unit
  if (c <  0) return GColorIndigo;          // freezing: cold violet
  if (c < 12) return GColorPictonBlue;      // cold: blue
  if (c < 22) return GColorMediumSpringGreen; // mild: the Aura accent green
  if (c < 31) return GColorRajah;           // warm: yellow/orange
  return GColorSunsetOrange;                // hot: red (readable stand-in for maroon)
}

const char *wx_glyph(uint8_t code, bool night) {
  switch (code) {
    case WX_CLEAR:    return night ? "\xEF\x80\xAE" : "\xEF\x80\x8D";  // f02e moon / f00d sun
    case WX_FEW:      return night ? "\xEF\x82\x86" : "\xEF\x80\x82";  // f086 / f002
    case WX_CLOUDY:   return "\xEF\x81\x81";                            // f041 cloud
    case WX_OVERCAST: return "\xEF\x80\x93";                            // f013 cloudy
    case WX_FOG:      return "\xEF\x80\x94";                            // f014 fog
    case WX_DRIZZLE:  return "\xEF\x80\x9C";                            // f01c sprinkle
    case WX_RAIN:     return "\xEF\x80\x99";                            // f019 rain
    case WX_HEAVY:    return "\xEF\x80\x98";                            // f018 rain-wind
    case WX_SNOW:     return "\xEF\x80\x9B";                            // f01b snow
    case WX_THUNDER:  return "\xEF\x80\x9E";                            // f01e thunderstorm
    default:          return "\xEF\x80\x82";
  }
}

// True if `t` (unix seconds) falls in night for the forecast's sun times.
static bool is_night_at(time_t t) {
  time_t sr = s_wx.sunrise, ss = s_wx.sunset;
  if (sr == 0 || ss == 0 || ss <= sr) return false;
  // Compare within the day only.
  struct tm *lt = localtime(&t);
  int mins = lt->tm_hour * 60 + lt->tm_min;
  struct tm srt = *localtime(&sr); struct tm sst = *localtime(&ss);
  int sr_m = srt.tm_hour * 60 + srt.tm_min;
  int ss_m = sst.tm_hour * 60 + sst.tm_min;
  return mins < sr_m || mins > ss_m;
}

// ---- text helper -----------------------------------------------------------

static void draw_text_in(GContext *ctx, const char *s, GFont f, GRect box,
                         GColor col, GTextAlignment align) {
  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, s, f, box, GTextOverflowModeTrailingEllipsis, align, NULL);
}

// Draw text with a 1px black halo so it stays legible floating over the scene
// (Pebble has no soft shadow/blur; four cardinal offsets read as a clean edge).
static void draw_text_halo(GContext *ctx, const char *s, GFont f, GRect box,
                           GColor col, GTextAlignment align) {
  graphics_context_set_text_color(ctx, GColorBlack);
  static const int dx[] = { -1, 1, 0, 0 }, dy[] = { 0, 0, -1, 1 };
  for (int i = 0; i < 4; i++) {
    graphics_draw_text(ctx, s, f, GRect(box.origin.x + dx[i], box.origin.y + dy[i],
                                        box.size.w, box.size.h),
                       GTextOverflowModeTrailingEllipsis, align, NULL);
  }
  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, s, f, box, GTextOverflowModeTrailingEllipsis, align, NULL);
}

// One-word-ish condition summary shown under the temperature (the hero headline).
static const char *wx_summary(uint8_t code) {
  switch (code) {
    case WX_CLEAR:    return "Clear";
    case WX_FEW:      return "Few clouds";
    case WX_CLOUDY:   return "Cloudy";
    case WX_OVERCAST: return "Overcast";
    case WX_FOG:      return "Fog";
    case WX_DRIZZLE:  return "Drizzle";
    case WX_RAIN:     return "Rain";
    case WX_HEAVY:    return "Heavy rain";
    case WX_SNOW:     return "Snow";
    case WX_THUNDER:  return "Thunderstorm";
    default:          return "";
  }
}

// ---- aviso / gauge vocabulary (docs/06) ------------------------------------

// The four AEMET colour levels, mirrored for the derived advisory.
static GColor alert_color(uint8_t lvl) {
  switch (lvl) {
    case 1:  return GColorGreen;    // verde
    case 2:  return GColorYellow;   // amarillo
    case 3:  return GColorOrange;   // naranja
    case 4:  return GColorRed;      // rojo
    default: return GColorBlack;
  }
}
// Black reads better on the light levels, white on the dark ones.
static GColor alert_ink(uint8_t lvl) { return (lvl == 2 || lvl == 3) ? GColorBlack : GColorWhite; }

static const char *alert_word(uint8_t label) {
  switch (label) {
    case ALERT_HEAT:  return "Heat";
    case ALERT_STORM: return "Storm";
    case ALERT_SNOW:  return "Snow";
    case ALERT_WIND:  return "Wind";
    case ALERT_FOG:   return "Fog";
    case ALERT_RAIN:  return "Rain";
    case ALERT_COLD:  return "Cold";
    default:          return "Warning";
  }
}
static const char *alert_level_word(uint8_t lvl) {
  switch (lvl) {
    case 1:  return "Advisory";
    case 2:  return "Yellow";
    case 3:  return "Orange";
    case 4:  return "Red";
    default: return "";
  }
}

// 16-point compass labels (index 0 = N, clockwise), matching the phone's dir16.
static const char *CARD16[16] = {
  "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
  "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
};

// Wind ramp (compare in km/h): light / moderate / strong / gale.
static GColor wind_color(int kmh) {
  if (kmh < 12) return GColorMediumSpringGreen;
  if (kmh < 39) return GColorRajah;             // yellow
  if (kmh < 62) return GColorOrange;
  return GColorRed;
}

// WHO UV bands and their ramp colours.
static GColor uv_color(int uv) {
  if (uv <= 2)  return GColorGreen;
  if (uv <= 5)  return GColorYellow;
  if (uv <= 7)  return GColorOrange;
  if (uv <= 10) return GColorRed;
  return GColorPurple;                          // extreme (violet)
}
static const char *uv_band(int uv) {
  if (uv <= 2)  return "Low";
  if (uv <= 5)  return "Moderate";
  if (uv <= 7)  return "High";
  if (uv <= 10) return "Very high";
  return "Extreme";
}

static const char *MOON_NAME[8] = {
  "New", "Waxing crescent", "First quarter", "Waxing gibbous",
  "Full", "Waning gibbous", "Last quarter", "Waning crescent",
};

// A radial gauge: a full track ring, then a `pct` arc filled clockwise from top.
static void draw_ring(GContext *ctx, GRect frame, int thickness, int pct,
                      GColor track, GColor fill) {
  graphics_context_set_fill_color(ctx, track);
  graphics_fill_radial(ctx, frame, GOvalScaleModeFitCircle, thickness, 0, TRIG_MAX_ANGLE);
  if (pct > 0) {
    if (pct > 100) pct = 100;
    graphics_context_set_fill_color(ctx, fill);
    graphics_fill_radial(ctx, frame, GOvalScaleModeFitCircle, thickness,
                         0, TRIG_MAX_ANGLE * pct / 100);
  }
}

static int isqrt_i(int v) {
  if (v <= 0) return 0;
  int x = v, y = (x + 1) / 2;
  while (y < x) { x = y; y = (x + v / x) / 2; }
  return x;
}

// A moon disc with a terminator: the lit fraction is a lune bounded by an
// ellipse of half-width (1 - 2*lit)*r, scanned row by row. `waxing` lights the
// right limb, waning the left; the unlit limb stays a dim disc so it still reads.
static void draw_moon(GContext *ctx, GPoint c, int r, int lit_pct, bool waxing) {
  for (int dy = -r; dy <= r; dy++) {
    int half = isqrt_i(r * r - dy * dy);
    int y = c.y + dy;
    graphics_context_set_stroke_color(ctx, GColorOxfordBlue);   // dark limb
    graphics_draw_line(ctx, GPoint(c.x - half, y), GPoint(c.x + half, y));
    int xt = (100 - 2 * lit_pct) * half / 100;                  // terminator x at this row
    int x0, x1;
    if (waxing) { x0 = xt; x1 = half; } else { x0 = -half; x1 = -xt; }
    if (x1 > x0) {
      graphics_context_set_stroke_color(ctx, GColorWhite);
      graphics_draw_line(ctx, GPoint(c.x + x0, y), GPoint(c.x + x1, y));
    }
  }
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_draw_circle(ctx, c, r);
}

// A warning triangle with an exclamation punched through it in `hole`.
static void draw_warning_triangle(GContext *ctx, GPoint c, int s, GColor fill, GColor hole) {
  GPoint pts[3] = { { c.x, c.y - s }, { c.x - s, c.y + s * 3 / 4 }, { c.x + s, c.y + s * 3 / 4 } };
  GPathInfo info = { 3, pts };
  GPath *p = gpath_create(&info);
  if (p) {
    graphics_context_set_fill_color(ctx, fill);
    gpath_draw_filled(ctx, p);
    gpath_destroy(p);
  }
  graphics_context_set_fill_color(ctx, hole);
  graphics_fill_rect(ctx, GRect(c.x - 2, c.y - s / 3, 4, s * 2 / 3 - 3), 1, GCornersAll);
  graphics_fill_rect(ctx, GRect(c.x - 2, c.y + s / 2 - 1, 4, 4), 1, GCornersAll);
}

// hh:mm for a unix time in the watch's 12/24h style.
static void fmt_hhmm(char *buf, size_t n, time_t t) {
  strftime(buf, n, clock_is_24h_style() ? "%H:%M" : "%I:%M", localtime(&t));
}

// ---- hero screen -----------------------------------------------------------

static void draw_hero(GContext *ctx, GRect b) {
  time_t now = time(NULL);
  // The sun arcs over the full frame; the scenery then occludes it near the
  // horizon, exactly as the phone hero composes sky -> disc -> landscape.
  SunState sun = sun_compute(now, s_wx.sunrise, s_wx.sunset, b);
  sky_draw(ctx, b, sun, s_wx.code);
  scene_draw(ctx, b, sun, s_wx.code);

  int Y = b.origin.y, W = b.size.w;
  int pad = 6, X = b.origin.x + pad, tw = W - pad * 2;
  char buf[32];

  // The Aura hero overlay floats over the scene (no card frame), left-aligned
  // like the phone app: location top-left, then the big temperature, condition
  // summary, hi/lo, and a warning pill for severe weather.

  // A tight left-aligned cluster near the top (like the phone hero). Each line's
  // box top is placed by hand so the *visible* gap between glyphs is equal: the
  // XL temp font carries heavy internal top padding, so its box is pulled up hard
  // (its digits render well below the box top).
  // Box tops solved from each font's measured glyph offsets so the *visible* gap
  // between every pair of lines is a uniform 7px (the big temp box sits high
  // because its digits render ~14px below the box top).
  int y_loc  = Y +  6;   // location -> glyphs 10..28
  int y_temp = Y + 21;   // big temp -> digits 35..69
  int y_sum  = Y + 72;   // summary  -> glyphs 76..90
  int y_hilo = Y + 93;   // hi/lo    -> glyphs 97..111

  // Location, top-left, small.
  draw_text_halo(ctx, s_wx.name, s_font_text, GRect(X, y_loc, tw, 20),
                 GColorWhite, GTextAlignmentLeft);

  // Current temperature, extra-large, tucked close under the location.
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", s_wx.temp);
  draw_text_halo(ctx, buf, s_font_xl, GRect(X, y_temp, tw, 56),
                 temp_color(s_wx.temp, s_wx.is_metric), GTextAlignmentLeft);

  // Condition summary under the temperature.
  draw_text_halo(ctx, wx_summary(s_wx.code), s_font_text,
                 GRect(X, y_sum, tw, 22), GColorWhite, GTextAlignmentLeft);

  // Hi / lo dataline below the summary.
  if (s_wx.updated == 0) {
    draw_text_halo(ctx, "no data", s_font_small, GRect(X, y_hilo + 1, tw, 16),
                   GColorLightGray, GTextAlignmentLeft);
  } else {
    snprintf(buf, sizeof(buf), "%d\xC2\xB0 / %d\xC2\xB0", s_wx.tmax, s_wx.tmin);
    draw_text_halo(ctx, buf, s_font_text, GRect(X, y_hilo, tw, 20),
                   GColorWhite, GTextAlignmentLeft);
  }

  // Warning pill over the scene, left-aligned, only when an aviso is active. The
  // phone fills alert_level/label (derived worldwide, official AEMET in Spain);
  // the full colour-coded aviso card carries the detail.
  if (s_wx.alert_level > 0) {
    const char *warn = alert_word(s_wx.alert_label);
    GSize sz = graphics_text_layout_get_content_size(warn, s_font_small,
                 GRect(0, 0, tw, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    int pw = sz.w + 18, ph = 18, pyy = y_hilo + 25;   // 6px under the hi/lo row
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(X, pyy, pw, ph), ph / 2, GCornersAll);
    draw_text_in(ctx, warn, s_font_small, GRect(X, pyy + 1, pw, 16),
                 alert_color(s_wx.alert_level), GTextAlignmentCenter);
  }
}

// A thin precip bar along the bottom of a row, width proportional to pop, shown
// only when rain is worth a glance (>= 20%). One drawn bar, never a chart.
static void draw_pop_bar(GContext *ctx, GRect row, uint8_t pop) {
  if (pop < 20) return;
  int w = row.size.w * pop / 100;
  graphics_context_set_fill_color(ctx, GColorPictonBlue);
  graphics_fill_rect(ctx, GRect(row.origin.x, row.origin.y + row.size.h - 2, w, 2),
                     0, GCornerNone);
}

// ---- hourly screen: next 8 hours -------------------------------------------

static void draw_hourly(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  time_t now = time(NULL);
  int now_hour = localtime(&now)->tm_hour;
  bool h24 = clock_is_24h_style();
  int row_h = b.size.h / HOURS_N;
  char buf[8];

  // Temperature span across the visible hours drives the trend-bar lengths.
  int lo = 127, hi = -128;
  for (int i = 0; i < HOURS_N; i++) {
    int t = s_wx.hours[i].temp;
    if (t < lo) lo = t;
    if (t > hi) hi = t;
  }
  int range = (hi - lo) < 1 ? 1 : (hi - lo);
  int trackX = b.origin.x + 72;
  int trackW = b.size.w - trackX - 46;
  int dotr = 5;
  int usable = trackW - 2 * dotr;

  for (int i = 0; i < HOURS_N; i++) {
    GRect row = GRect(b.origin.x, b.origin.y + i * row_h, b.size.w, row_h);
    HourSlot *hs = &s_wx.hours[i];
    int hour = (now_hour + i) % 24;
    int cy = row.origin.y + row_h / 2;
    GColor tint = temp_color(hs->temp, s_wx.is_metric);

    // Hour label, 12/24h per the watch's system setting.
    if (h24)               snprintf(buf, sizeof(buf), "%02d", hour);
    else                   snprintf(buf, sizeof(buf), "%d%s", (hour % 12) ? (hour % 12) : 12,
                                    hour < 12 ? "a" : "p");
    draw_text_in(ctx, buf, s_font_text, GRect(row.origin.x + 6, row.origin.y, 28, row_h),
                 GColorWhite, GTextAlignmentLeft);

    // Condition glyph, night variant if that hour is after sunset / before sunrise.
    bool night = is_night_at(now + i * 3600);
    draw_text_in(ctx, wx_glyph(hs->code, night), s_font_wx_sm,
                 GRect(row.origin.x + 34, row.origin.y - 2, 34, row_h),
                 GColorWhite, GTextAlignmentCenter);

    // Trend track spans the day's min..max; a ramp-tinted dot marks this hour.
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, GRect(trackX, cy - 1, trackW, 2), 1, GCornersAll);
    int dotx = trackX + dotr + (hs->temp - lo) * usable / range;
    graphics_context_set_fill_color(ctx, tint);
    graphics_fill_circle(ctx, GPoint(dotx, cy), dotr);

    // Temperature, tinted by the ramp.
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", hs->temp);
    draw_text_in(ctx, buf, s_font_text, GRect(row.origin.x, row.origin.y, row.size.w - 6, row_h),
                 tint, GTextAlignmentRight);

    draw_pop_bar(ctx, row, hs->pop);
  }
}

// ---- daily screen: next 6 days ---------------------------------------------

static void draw_daily(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  time_t now = time(NULL);
  int row_h = b.size.h / DAYS_N;
  char buf[8];

  for (int i = 0; i < DAYS_N; i++) {
    GRect row = GRect(b.origin.x, b.origin.y + i * row_h, b.size.w, row_h);
    DaySlot *ds = &s_wx.days[i];

    // Weekday, three letters, derived from today plus the slot index.
    time_t day = now + i * 86400;
    strftime(buf, sizeof(buf), "%a", localtime(&day));
    draw_text_in(ctx, buf, s_font_text, GRect(row.origin.x + 6, row.origin.y, 52, row_h),
                 GColorWhite, GTextAlignmentLeft);

    // Condition glyph.
    draw_text_in(ctx, wx_glyph(ds->code, false), s_font_wx_sm,
                 GRect(row.origin.x + row.size.w / 2 - 20, row.origin.y - 2, 40, row_h),
                 GColorWhite, GTextAlignmentCenter);

    // Max at the right edge, min just left of it, each tinted by the ramp so a
    // cold day reads blue and a hot day red without any label.
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", ds->max);
    draw_text_in(ctx, buf, s_font_text, GRect(row.origin.x, row.origin.y, row.size.w - 8, row_h),
                 temp_color(ds->max, s_wx.is_metric), GTextAlignmentRight);
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", ds->min);
    draw_text_in(ctx, buf, s_font_text, GRect(row.origin.x, row.origin.y, row.size.w - 52, row_h),
                 temp_color(ds->min, s_wx.is_metric), GTextAlignmentRight);

    draw_pop_bar(ctx, row, ds->pop);
  }
}

// ---- aviso card: colour-coded warning, only when one is active -------------

static void draw_aviso(GContext *ctx, GRect b) {
  uint8_t lvl = s_wx.alert_level;
  GColor bg = alert_color(lvl), ink = alert_ink(lvl);
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  GPoint c = GPoint(b.origin.x + b.size.w / 2, b.origin.y + 58);
  draw_warning_triangle(ctx, c, 34, ink, bg);

  draw_text_in(ctx, alert_word(s_wx.alert_label), s_font_big,
               GRect(b.origin.x, b.origin.y + 100, b.size.w, 42), ink, GTextAlignmentCenter);
  draw_text_in(ctx, alert_level_word(lvl), s_font_text,
               GRect(b.origin.x, b.origin.y + 148, b.size.w, 22), ink, GTextAlignmentCenter);
}

// ---- sun & moon card: daylight arc by day, moon phase after dark ------------

static void draw_sunmoon(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  time_t now = time(NULL);
  bool night = is_night_at(now);
  int cx = b.origin.x + b.size.w / 2;
  char buf[16];

  if (!night) {
    draw_text_in(ctx, "Daylight", s_font_text,
                 GRect(b.origin.x, b.origin.y + 8, b.size.w, 22), GColorWhite, GTextAlignmentCenter);

    int r = b.size.w / 2 - 22;
    GRect af = GRect(cx - r, b.origin.y + 52, 2 * r, 2 * r);   // sun rides the top half
    graphics_context_set_stroke_color(ctx, GColorDarkGray);
    graphics_context_set_stroke_width(ctx, 3);
    graphics_draw_arc(ctx, af, GOvalScaleModeFitCircle, DEG_TO_TRIGANGLE(-90), DEG_TO_TRIGANGLE(90));

    int prog = 0;
    if (s_wx.sunset > s_wx.sunrise) {
      prog = (int)((now - s_wx.sunrise) * 100 / (s_wx.sunset - s_wx.sunrise));
      if (prog < 0) prog = 0;
      if (prog > 100) prog = 100;
    }
    int32_t ang = DEG_TO_TRIGANGLE(-90 + 180 * prog / 100);    // sunrise left, noon top, sunset right
    GPoint sp = gpoint_from_polar(af, GOvalScaleModeFitCircle, ang);
    graphics_context_set_fill_color(ctx, GColorYellow);
    graphics_fill_circle(ctx, sp, 7);

    time_t sr = s_wx.sunrise, ss = s_wx.sunset;
    fmt_hhmm(buf, sizeof(buf), sr);
    draw_text_in(ctx, buf, s_font_small, GRect(cx - r - 4, af.origin.y + 2 * r - 6, 52, 16),
                 GColorChromeYellow, GTextAlignmentLeft);
    fmt_hhmm(buf, sizeof(buf), ss);
    draw_text_in(ctx, buf, s_font_small, GRect(cx + r - 48, af.origin.y + 2 * r - 6, 52, 16),
                 GColorOrange, GTextAlignmentRight);
  } else {
    draw_text_in(ctx, "Moon", s_font_text,
                 GRect(b.origin.x, b.origin.y + 8, b.size.w, 22), GColorWhite, GTextAlignmentCenter);
    draw_moon(ctx, GPoint(cx, b.origin.y + 84), 36, s_wx.moon_illum, s_wx.moon_phase < 4);
    draw_text_in(ctx, MOON_NAME[s_wx.moon_phase & 7], s_font_text,
                 GRect(b.origin.x, b.origin.y + 132, b.size.w, 22), GColorWhite, GTextAlignmentCenter);
    snprintf(buf, sizeof(buf), "%d%% lit", s_wx.moon_illum);
    draw_text_in(ctx, buf, s_font_small, GRect(b.origin.x, b.origin.y + 158, b.size.w, 16),
                 GColorLightGray, GTextAlignmentCenter);
  }
}

// ---- wind card: a needle over a compass rose --------------------------------

static void draw_wind(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  draw_text_in(ctx, "Wind", s_font_text,
               GRect(b.origin.x, b.origin.y + 8, b.size.w, 22), GColorWhite, GTextAlignmentCenter);

  int cx = b.origin.x + b.size.w / 2, cy = b.origin.y + 78, r = 46;
  GRect rf = GRect(cx - r, cy - r, 2 * r, 2 * r);
  graphics_context_set_stroke_color(ctx, GColorDarkGray);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_circle(ctx, GPoint(cx, cy), r);
  draw_text_in(ctx, "N", s_font_small, GRect(cx - 8, cy - r - 3, 16, 16), GColorLightGray, GTextAlignmentCenter);
  draw_text_in(ctx, "S", s_font_small, GRect(cx - 8, cy + r - 13, 16, 16), GColorLightGray, GTextAlignmentCenter);
  draw_text_in(ctx, "E", s_font_small, GRect(cx + r - 15, cy - 8, 16, 16), GColorLightGray, GTextAlignmentCenter);
  draw_text_in(ctx, "W", s_font_small, GRect(cx - r - 1, cy - 8, 16, 16), GColorLightGray, GTextAlignmentCenter);

  int kmh = s_wx.is_metric ? s_wx.wind_speed : (int)(s_wx.wind_speed * 1.609f);
  GColor wc = wind_color(kmh);
  int32_t ang = DEG_TO_TRIGANGLE(s_wx.wind_dir * 360 / 16);          // the source (tail)
  GPoint tail = gpoint_from_polar(rf, GOvalScaleModeFitCircle, ang);
  GPoint tip  = gpoint_from_polar(rf, GOvalScaleModeFitCircle,
                                  (ang + TRIG_MAX_ANGLE / 2) % TRIG_MAX_ANGLE);
  graphics_context_set_stroke_color(ctx, wc);
  graphics_context_set_stroke_width(ctx, 4);
  graphics_draw_line(ctx, tail, tip);
  graphics_context_set_fill_color(ctx, wc);
  graphics_fill_circle(ctx, tip, 5);                                 // arrow head (where it blows)
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(cx, cy), 3);

  char buf[24];
  snprintf(buf, sizeof(buf), "%d %s", s_wx.wind_speed, s_wx.is_metric ? "km/h" : "mph");
  draw_text_in(ctx, buf, s_font_big, GRect(b.origin.x, cy + r + 6, b.size.w, 34), wc, GTextAlignmentCenter);
  snprintf(buf, sizeof(buf), "from %s   gust %d", CARD16[s_wx.wind_dir & 15], s_wx.wind_gust);
  draw_text_in(ctx, buf, s_font_small, GRect(b.origin.x, cy + r + 42, b.size.w, 16),
               GColorLightGray, GTextAlignmentCenter);
}

// ---- UV card: a ring from 0 to today's peak ---------------------------------

static void draw_uv(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  draw_text_in(ctx, "UV index", s_font_text,
               GRect(b.origin.x, b.origin.y + 8, b.size.w, 22), GColorWhite, GTextAlignmentCenter);

  int cx = b.origin.x + b.size.w / 2, cy = b.origin.y + 96, r = 50;
  GRect rf = GRect(cx - r, cy - r, 2 * r, 2 * r);
  int peak = s_wx.uv_peak > 0 ? s_wx.uv_peak : 1;
  int pct = s_wx.uv * 100 / peak;
  GColor uc = uv_color(s_wx.uv);
  draw_ring(ctx, rf, 10, pct, GColorDarkGray, uc);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d", s_wx.uv);
  draw_text_in(ctx, buf, s_font_big, GRect(cx - 40, cy - 24, 80, 36), uc, GTextAlignmentCenter);
  draw_text_in(ctx, uv_band(s_wx.uv), s_font_text,
               GRect(b.origin.x, cy + r + 6, b.size.w, 22), GColorWhite, GTextAlignmentCenter);
  snprintf(buf, sizeof(buf), "peak %d", s_wx.uv_peak);
  draw_text_in(ctx, buf, s_font_small, GRect(b.origin.x, cy + r + 30, b.size.w, 16),
               GColorLightGray, GTextAlignmentCenter);
}

// ---- details card: the fields the hero omits, to stay calm ------------------

static void detail_row(GContext *ctx, GRect b, int y, const char *label, const char *val, GColor vc) {
  draw_text_in(ctx, label, s_font_text, GRect(b.origin.x + 16, y, b.size.w / 2, 26),
               GColorLightGray, GTextAlignmentLeft);
  draw_text_in(ctx, val, s_font_text, GRect(b.origin.x, y, b.size.w - 16, 26),
               vc, GTextAlignmentRight);
}

static void draw_details(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  draw_text_in(ctx, "Details", s_font_text,
               GRect(b.origin.x, b.origin.y + 8, b.size.w, 22), GColorWhite, GTextAlignmentCenter);

  int y = b.origin.y + 44, rh = 40;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", s_wx.feels_like);
  detail_row(ctx, b, y, "Feels", buf, temp_color(s_wx.feels_like, s_wx.is_metric));
  snprintf(buf, sizeof(buf), "%d%%", s_wx.humidity);
  detail_row(ctx, b, y + rh, "Humidity", buf, GColorWhite);
  snprintf(buf, sizeof(buf), "%d mm", s_wx.precip_mm);
  detail_row(ctx, b, y + rh * 2, "Precip", buf, GColorPictonBlue);
  int kmh = s_wx.is_metric ? s_wx.wind_gust : (int)(s_wx.wind_gust * 1.609f);
  snprintf(buf, sizeof(buf), "%d %s", s_wx.wind_gust, s_wx.is_metric ? "km/h" : "mph");
  detail_row(ctx, b, y + rh * 3, "Gust", buf, wind_color(kmh));
}

// ---- bulletin card: Aura's plain-language forecast --------------------------
// Aura's own summary worldwide (generated on the phone from the numbers), the
// official AEMET boletin in Spain. The watch only word-wraps the text it is sent.

static void draw_bulletin(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  draw_text_in(ctx, "Forecast", s_font_text,
               GRect(b.origin.x, b.origin.y + 8, b.size.w, 22), GColorWhite, GTextAlignmentCenter);

  // A left-aligned, word-wrapped block under the title. Font 14 fits ~200 chars
  // in the body height; longer prose (a full AEMET boletin) is capped on phone.
  GRect body = GRect(b.origin.x + 12, b.origin.y + 40, b.size.w - 24, b.size.h - 48);
  const char *s = s_bulletin[0] ? s_bulletin : "No forecast yet.";
  draw_text_in(ctx, s, s_font_small, body, GColorWhite, GTextAlignmentLeft);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  switch (s_card) {
    case CARD_HERO:     draw_hero(ctx, b);     break;
    case CARD_AVISO:    draw_aviso(ctx, b);    break;
    case CARD_HOURLY:   draw_hourly(ctx, b);   break;
    case CARD_DAILY:    draw_daily(ctx, b);    break;
    case CARD_SUNMOON:  draw_sunmoon(ctx, b);  break;
    case CARD_WIND:     draw_wind(ctx, b);     break;
    case CARD_UV:       draw_uv(ctx, b);       break;
    case CARD_DETAILS:  draw_details(ctx, b);  break;
    case CARD_BULLETIN: draw_bulletin(ctx, b); break;
  }
}

// ---- input + ticks ---------------------------------------------------------

// A card shows only when it has something to say: the aviso needs an active
// warning, the UV ring needs a day with some sun. The rest are always present.
static bool card_visible(int c) {
  switch (c) {
    case CARD_AVISO:    return s_wx.alert_level > 0;
    case CARD_UV:       return s_wx.uv_peak > 0;
    case CARD_BULLETIN: return s_bulletin[0] != '\0';
    default:            return true;
  }
}

// Step to the next visible card in `dir` (+1 down, -1 up), wrapping.
static void card_step(int dir) {
  int c = s_card;
  for (int i = 0; i < CARD_N; i++) {
    c = (c + dir + CARD_N) % CARD_N;
    if (card_visible(c)) { s_card = c; break; }
  }
  layer_mark_dirty(s_canvas);
}

static void up_click(ClickRecognizerRef r, void *ctx)   { card_step(-1); }
static void down_click(ClickRecognizerRef r, void *ctx) { card_step(1); }
static void select_click(ClickRecognizerRef r, void *ctx) {
  request_refresh();
  layer_mark_dirty(s_canvas);
}
static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
}

static void tick_handler(struct tm *t, TimeUnits units) {
  // The hero and the sun/moon card both animate the sun's position each minute.
  if (s_card == CARD_HERO || s_card == CARD_SUNMOON) layer_mark_dirty(s_canvas);
}

// ---- persistence + first-run default ---------------------------------------

static void persist_save(void) {
  persist_write_data(PKEY_WEATHER, &s_wx, sizeof(s_wx));
}

// A sensible first-run state so the signature shows before any phone data:
// today's default sun times draw the arc, updated == 0 reads as "no data".
static void seed_default(void) {
  memset(&s_wx, 0, sizeof(s_wx));
  strncpy(s_wx.name, "Aura", sizeof(s_wx.name) - 1);
  s_wx.code = WX_FEW; s_wx.is_metric = 1; s_wx.updated = 0;
  time_t now = time(NULL);
  struct tm lt = *localtime(&now);
  lt.tm_hour = 7;  lt.tm_min = 0; lt.tm_sec = 0; s_wx.sunrise = mktime(&lt);
  lt.tm_hour = 21; lt.tm_min = 0;                s_wx.sunset  = mktime(&lt);
}

static void load_weather(void) {
  if (persist_exists(PKEY_WEATHER) &&
      persist_get_size(PKEY_WEATHER) == (int)sizeof(s_wx)) {
    persist_read_data(PKEY_WEATHER, &s_wx, sizeof(s_wx));
  } else {
    seed_default();
  }
  if (persist_exists(PKEY_BULLETIN)) {
    persist_read_string(PKEY_BULLETIN, s_bulletin, sizeof(s_bulletin));
  }
}

// ---- AppMessage inbox ------------------------------------------------------

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;

  // Bulletin frame (its own message, sent last): the forecast prose.
  if ((t = dict_find(iter, MESSAGE_KEY_WX_BULL))) {
    strncpy(s_bulletin, t->value->cstring, sizeof(s_bulletin) - 1);
    s_bulletin[sizeof(s_bulletin) - 1] = '\0';
    persist_write_string(PKEY_BULLETIN, s_bulletin);
    layer_mark_dirty(s_canvas);
    return;
  }

  // Current / handshake message.
  if ((t = dict_find(iter, MESSAGE_KEY_WX_OK))) {
    if (t->value->int32 == 0) return;   // provider error: keep the persisted struct
    Tuple *tp;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_NAME)))
      strncpy(s_wx.name, tp->value->cstring, sizeof(s_wx.name) - 1);
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_TEMP)))    s_wx.temp     = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_TMIN)))    s_wx.tmin     = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_TMAX)))    s_wx.tmax     = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_CODE)))    s_wx.code     = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_HUM)))     s_wx.humidity = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_POP)))     s_wx.pop      = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_SUNRISE))) s_wx.sunrise  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_SUNSET)))  s_wx.sunset   = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_UNITS)))   s_wx.is_metric = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_UPDATED))) s_wx.updated  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_FEELS)))   s_wx.feels_like  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_WIND)))    s_wx.wind_speed  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_WDIR)))    s_wx.wind_dir    = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_GUST)))    s_wx.wind_gust   = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_PRECIP)))  s_wx.precip_mm   = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_STORM)))   s_wx.storm_prob  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_UV)))      s_wx.uv          = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_UVPEAK)))  s_wx.uv_peak     = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_AQI)))     s_wx.aqi         = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_MOON)))    s_wx.moon_phase  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_MOONILL))) s_wx.moon_illum  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_ALEVEL)))  s_wx.alert_level = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_ALABEL)))  s_wx.alert_label = tp->value->int32;
    persist_save();
    if (!card_visible(s_card)) s_card = CARD_HERO;   // the open card may have just vanished
    layer_mark_dirty(s_canvas);
    return;
  }

  // Hourly frame: one slot per message, keyed by index.
  if ((t = dict_find(iter, MESSAGE_KEY_H_IDX))) {
    int i = t->value->int32;
    if (i >= 0 && i < HOURS_N) {
      Tuple *tp;
      if ((tp = dict_find(iter, MESSAGE_KEY_H_TEMP))) s_wx.hours[i].temp = tp->value->int32;
      if ((tp = dict_find(iter, MESSAGE_KEY_H_CODE))) s_wx.hours[i].code = tp->value->int32;
      if ((tp = dict_find(iter, MESSAGE_KEY_H_POP)))  s_wx.hours[i].pop  = tp->value->int32;
    }
    layer_mark_dirty(s_canvas);
    return;
  }

  // Daily frame: one slot per message. Persist once the last day lands.
  if ((t = dict_find(iter, MESSAGE_KEY_D_IDX))) {
    int i = t->value->int32;
    if (i >= 0 && i < DAYS_N) {
      Tuple *tp;
      if ((tp = dict_find(iter, MESSAGE_KEY_D_MIN)))  s_wx.days[i].min  = tp->value->int32;
      if ((tp = dict_find(iter, MESSAGE_KEY_D_MAX)))  s_wx.days[i].max  = tp->value->int32;
      if ((tp = dict_find(iter, MESSAGE_KEY_D_CODE))) s_wx.days[i].code = tp->value->int32;
      if ((tp = dict_find(iter, MESSAGE_KEY_D_POP)))  s_wx.days[i].pop  = tp->value->int32;
    }
    if (i == DAYS_N - 1) persist_save();
    layer_mark_dirty(s_canvas);
    return;
  }
}

// Ask the phone for a fresh forecast (SELECT, and on launch the phone self-starts).
static void request_refresh(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) == APP_MSG_OK) {
    dict_write_uint8(out, MESSAGE_KEY_REFRESH, 1);
    app_message_outbox_send();
  }
}

// ---- lifecycle -------------------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);
  s_canvas = layer_create(b);
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
}

static void init(void) {
  s_font_xl     = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_AURA_48));
  s_font_big    = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_AURA_34));
  s_font_text   = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_AURA_18));
  s_font_small  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_AURA_14));
  s_font_wx     = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_WI_30));
  s_font_wx_sm  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_WI_20));

  load_weather();

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load, .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  app_message_register_inbox_received(inbox_received);
  app_message_open(768, 64);   // inbox holds the fuller current frame (name + 25 ints)
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
  fonts_unload_custom_font(s_font_xl);
  fonts_unload_custom_font(s_font_big);
  fonts_unload_custom_font(s_font_text);
  fonts_unload_custom_font(s_font_small);
  fonts_unload_custom_font(s_font_wx);
  fonts_unload_custom_font(s_font_wx_sm);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
