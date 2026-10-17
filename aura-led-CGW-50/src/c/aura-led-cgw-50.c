// Casio CGW-50 "Cosmo Phase" homage for the Pebble Time 2 (emery, 200 x 228).
// Layout: status row + daylight bar on top, the time in the middle, weather and a 12 hour
// forecast chart below. The theme turns negative after sunset. Settings and weather come
// from the phone over AppMessage (see src/pkjs). Display-only: watchfaces get no touch events.
#include <pebble.h>
#include "lcd.h"
#include "message_keys.auto.h"

#define W 200
#define H 228
#define PKEY_THEME   1
#define PKEY_DAY_BG  2
#define PKEY_DAY_FG  3
#define PKEY_NIGHT_BG 4
#define PKEY_NIGHT_FG 5
#define PKEY_ACCENT  6
#define PKEY_TIME24  7
#define PKEY_MDY     8
#define PKEY_GHOSTS  9
#define PKEY_WX_TEMP 10
#define PKEY_WX_CODE 11
#define PKEY_WX_OK   12
#define PKEY_WX_HI   13
#define PKEY_WX_LO   14
#define PKEY_SUNRISE 15
#define PKEY_SUNSET  16
#define PKEY_FC      17   // forecast blob

#define PKEY_LAYOUT 21
#define PKEY_DATESTYLE 22
#define PKEY_PRESET 23

#define FC_N 18             // hours sent by the phone; 12 are shown
#define FC_SHOW 12


// Layout ids are stored on the phone and the watch, so they keep their numbers: 0 quiet, 2 timeline,
// 3 stack. Id 1 was the Panel layout, now retired; a stored 1 is treated as quiet.
typedef struct { int date_y, strip_base, time_y, time_w, time_h, hair_y, bot_y, mx, step, labels; } Geo;
static const Geo GEO[4] = {
  { -1,  31,  71, 34, 82, 154, 158,  8, 7, 1 },   // quiet
  { -1,  31,  71, 34, 82, 154, 158,  8, 7, 1 },   // 1: retired, never selected
  { -2, 122,  32, 35, 86, 0,   156,  8, 0, 1 },   // timeline
  {  0, 224,   6, 60, 92, 0,     0,  8, 0, 0 },   // stack
};
static int s_date_style = 4;
static int s_layout;
static GColor s_body;
#define G GEO[s_layout]

typedef enum { THEME_AUTO, THEME_DAY, THEME_NIGHT } Theme;

// Settings (defaults match src/pkjs/config.js)
static int s_theme = THEME_AUTO;
static int s_preset = 0;   // 0 = custom colours below, else a PRESETS row
static int s_day_bg = 0, s_day_fg = 1, s_night_bg = 1, s_night_fg = 12, s_accent_pref = -1;
static bool s_time24 = true, s_mdy = true, s_ghosts = true;

// Weather and sun times from the phone; sun times are minutes since local midnight
static int s_wx_temp, s_wx_code, s_wx_hi, s_wx_lo;
static bool s_wx_ok;
static int s_sunrise = 450, s_sunset = 1170;

// Resolved colours for the current theme
static GColor s_bg, s_fg, s_accent, s_earth;
static bool s_night;

static Window *s_window;
static Layer *s_canvas;
static GFont s_f_wx, s_f_date;

static const char *DAYS[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char *MONTHS[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

// ---------------------------------------------------------------------------
// Theme

static GColor palette(int i) {
  switch (i) {
    case 0:  return GColorWhite;
    case 1:  return GColorBlack;
    case 2:  return GColorLightGray;
    case 3:  return GColorDarkGray;
    case 4:  return GColorRed;
    case 5:  return GColorOrange;
    case 6:  return GColorYellow;
    case 7:  return GColorGreen;
    case 8:  return GColorCyan;
    case 9:  return GColorBlue;
    case 10: return GColorPurple;
    case 11: return GColorPastelYellow;
    default: return GColorChromeYellow;
  }
}

static int luma(GColor c) { return c.r * 299 + c.g * 587 + c.b * 114; }   // 0..3000

static GColor mix(GColor a, GColor b, int pct_a) {
  return (GColor){ .a = 3,
                   .r = (a.r * pct_a + b.r * (100 - pct_a) + 50) / 100,
                   .g = (a.g * pct_a + b.g * (100 - pct_a) + 50) / 100,
                   .b = (a.b * pct_a + b.b * (100 - pct_a) + 50) / 100 };
}

static void set_aa(GColor c) {   // shades for the segment renderer, from c towards the background
  s_aa[0] = mix(c, s_bg, 30).argb;
  s_aa[1] = mix(c, s_bg, 62).argb;
  s_aa[2] = c.argb;
}

// The Aura sky ramp (see docs/PALETTE.md), darkened where needed to read on white.
static GColor sky_accent(int hour) {
  if (hour < 5)  return GColorIndigo;
  if (hour < 7)  return GColorFashionMagenta;
  if (hour < 10) return GColorBlueMoon;
  if (hour < 15) return GColorCobaltBlue;
  if (hour < 18) return GColorOrange;
  if (hour < 21) return GColorFolly;
  return GColorIndigo;
}

// Pre-built themes: day bg/fg/accent then night bg/fg/accent. Row 0 is unused (custom
// colours); the last id follows the weather by picking one of the mood rows.
typedef struct { uint8_t dbg, dfg, dacc, nbg, nfg, nacc; } Preset;
enum { P_SUNNY = 7, P_RAIN, P_STORM, P_SNOW, P_FOG, P_SUNSET, P_FOLLOW };
static const Preset PRESETS[] = {
  { 0 },
  { GColorMintGreenARGB8,      GColorBlackARGB8,      GColorDarkGreenARGB8,       GColorDarkGreenARGB8, GColorMintGreenARGB8,   GColorSpringBudARGB8 },     // Classic LCD
  { GColorWhiteARGB8,          GColorBlackARGB8,      GColorDarkCandyAppleRedARGB8, GColorBlackARGB8,   GColorWhiteARGB8,       GColorYellowARGB8 },        // High contrast
  { GColorYellowARGB8,         GColorBlackARGB8,      GColorDukeBlueARGB8,        GColorBlackARGB8,     GColorYellowARGB8,      GColorWhiteARGB8 },         // Black on yellow
  { GColorWhiteARGB8,          GColorBlackARGB8,      GColorCobaltBlueARGB8,      GColorBlackARGB8,     GColorWhiteARGB8,       GColorVividCeruleanARGB8 }, // Paper and ink
  { GColorPastelYellowARGB8,   GColorBlackARGB8,      GColorDarkCandyAppleRedARGB8, GColorBlackARGB8,   GColorChromeYellowARGB8, GColorOrangeARGB8 },      // Sand and amber
  { GColorCelesteARGB8,        GColorOxfordBlueARGB8, GColorBlueMoonARGB8,        GColorOxfordBlueARGB8, GColorCelesteARGB8,    GColorVividCeruleanARGB8 }, // Sky
  { GColorIcterineARGB8,       GColorBlackARGB8,      GColorOrangeARGB8,          GColorOxfordBlueARGB8, GColorChromeYellowARGB8, GColorYellowARGB8 },     // Sunny
  { GColorDukeBlueARGB8,       GColorWhiteARGB8,      GColorElectricBlueARGB8,    GColorOxfordBlueARGB8, GColorCelesteARGB8,    GColorVividCeruleanARGB8 }, // Rain
  { GColorImperialPurpleARGB8, GColorPastelYellowARGB8, GColorYellowARGB8,        GColorBlackARGB8,     GColorPastelYellowARGB8, GColorYellowARGB8 },      // Storm
  { GColorWhiteARGB8,          GColorOxfordBlueARGB8, GColorVividCeruleanARGB8,   GColorBlackARGB8,     GColorWhiteARGB8,       GColorCelesteARGB8 },       // Snow
  { GColorLightGrayARGB8,      GColorBlackARGB8,      GColorDarkGrayARGB8,        GColorDarkGrayARGB8,  GColorWhiteARGB8,       GColorLightGrayARGB8 },     // Fog
  { GColorOrangeARGB8,         GColorBlackARGB8,      GColorDarkCandyAppleRedARGB8, GColorIndigoARGB8,  GColorPastelYellowARGB8, GColorOrangeARGB8 },       // Sunset
};

static int mood_for_code(int code) {
  if (code <= 2)                                  return P_SUNNY;
  if (code == 3 || code == 45 || code == 48)      return P_FOG;
  if (code >= 95)                                 return P_STORM;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return P_SNOW;
  return P_RAIN;
}

static bool sun_is_down(int now_min) { return now_min < s_sunrise || now_min >= s_sunset; }

static void apply_theme(struct tm *t) {
  int now_min = t->tm_hour * 60 + t->tm_min;
  s_night = s_theme == THEME_NIGHT || (s_theme == THEME_AUTO && sun_is_down(now_min));
  int pid = s_preset == P_FOLLOW ? mood_for_code(s_wx_code) : s_preset;
  const Preset *pr = pid > 0 && pid < (int)(sizeof PRESETS / sizeof PRESETS[0]) ? &PRESETS[pid] : NULL;
  if (pr) {
    s_bg = (GColor){ .argb = s_night ? pr->nbg : pr->dbg };
    s_fg = (GColor){ .argb = s_night ? pr->nfg : pr->dfg };
  } else {
    s_bg = palette(s_night ? s_night_bg : s_day_bg);
    s_fg = palette(s_night ? s_night_fg : s_day_fg);
  }
  // Never let a poor pick make the time unreadable: keep a minimum luminance gap
  if (abs(luma(s_fg) - luma(s_bg)) < 1500) s_fg = luma(s_bg) > 1500 ? GColorBlack : GColorWhite;
  if (s_accent_pref >= 0) s_accent = palette(s_accent_pref);
  else if (pr) s_accent = (GColor){ .argb = s_night ? pr->nacc : pr->dacc };
  else s_accent = s_night ? GColorOrange : sky_accent(t->tm_hour);
  if (abs(luma(s_accent) - luma(s_bg)) < 600) s_accent = s_fg;
  s_earth = s_night ? GColorRed : GColorDarkCandyAppleRed;
  if (abs(luma(s_earth) - luma(s_bg)) < 600) s_earth = s_fg;

  s_ink = s_fg;
  s_ghost = mix(s_fg, s_bg, 12);
  s_ghost_density = 16;
  s_body = s_bg;
  s_aa[0] = mix(s_fg, s_bg, 30).argb;
  s_aa[1] = mix(s_fg, s_bg, 62).argb;
  s_aa[2] = s_fg.argb;
  window_set_background_color(s_window, s_body);
}

// ---------------------------------------------------------------------------
// Drawing helpers

static void draw_text(GContext *ctx, const char *s, GFont f, GRect box, GTextAlignment al) {
  graphics_draw_text(ctx, s, f, box, GTextOverflowModeFill, al, NULL);
}

static void tri(GContext *ctx, int x, int y, bool up) {   // 7 x 6 triangle with its left edge at x
  for (int r = 0; r < 6; r++) {
    int w = 1 + (up ? r : 5 - r) * 6 / 5;
    graphics_draw_line(ctx, GPoint(x + 3 - w / 2, y + r), GPoint(x + 3 - w / 2 + w - 1, y + r));
  }
}

// Weekday in the accent colour, then the date, as large as fits in `avail` px:
// "Sat, 17 October" in 22 px, then in 18 px, then "Sat 17 Oct" in 22 px.
// 5 x 7 dot-matrix capitals and digits (the Casio calculator-watch lettering), rows top to bottom.
static const char DOT_CHARS[] = "ABCDEFGHIJLMNOPRSTUVWY0123456789";
static const uint8_t DOT_GLYPHS[][7] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
  {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
  {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
  {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
  {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},
  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
  {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}, {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
};

// Draws `text` as 2 px dots on a 3 px pitch; unlit dots show as a faint tint when ghosts are on.
// Returns the x after the last character.
static int draw_dots(GContext *ctx, const char *text, int x, int y, GColor on, int dot) {
  const int pitch = 3;
  for (const char *c = text; *c; c++) {
    const char *at = *c == ' ' ? NULL : strchr(DOT_CHARS, *c);
    if (!at) { x += 3 * pitch; continue; }
    const uint8_t *g = DOT_GLYPHS[at - DOT_CHARS];
    for (int r = 0; r < 7; r++) {
      for (int col = 0; col < 5; col++) {
        bool lit = g[r] & (0x10 >> col);
        if (!lit && (!s_ghosts || dot > 2)) continue;
        graphics_context_set_fill_color(ctx, lit ? on : mix(s_fg, s_bg, 12));
        graphics_fill_rect(ctx, GRect(x + col * pitch, y + r * pitch, dot, dot), 0, GCornerNone);
      }
    }
    x += 6 * pitch;
  }
  return x;
}

// "SAT OCT 17" (or "SAT 17 OCT"), upper case, weekday in the accent colour.
static void draw_date(GContext *ctx, struct tm *tm, int x, int y) {
  char day[4], rest[12];
  bool dots = s_date_style >= 1 && s_date_style <= 3 && s_layout < 2;
  bool seg14 = s_date_style == 4 && s_layout < 2;
  for (int i = 0; i < 3; i++) day[i] = DAYS[tm->tm_wday][i] & ~0x20;
  day[3] = 0;
  char mon[4];
  for (int i = 0; i < 3; i++) mon[i] = MONTHS[tm->tm_mon][i] & ~0x20;
  mon[3] = 0;
  if (s_mdy) snprintf(rest, sizeof(rest), " %s %d", mon, tm->tm_mday);
  else snprintf(rest, sizeof(rest), " %d %s", tm->tm_mday, mon);
  if (dots) {
    int dot = s_date_style == 1 ? 2 : 3;
    int nx = draw_dots(ctx, day, x, y + 7, s_accent, dot);
    if (s_date_style < 3) { draw_dots(ctx, rest, nx, y + 7, s_fg, dot); return; }
    // hybrid: letters in dots, the day number in the same 7-segment digits as the time
    char num[4];
    snprintf(num, sizeof(num), "%d", tm->tm_mday);
    s_ghosts_on = false;
    s_ink = s_fg;
    if (s_mdy) {
      nx = draw_dots(ctx, " ", nx, y + 7, s_fg, dot);
      nx = draw_dots(ctx, mon, nx, y + 7, s_fg, dot) + 9;
      lcd_str(ctx, num, nx, y + 7, 11, 21, 3);
    } else {
      nx += 9;
      lcd_str(ctx, num, nx, y + 7, 11, 21, 3);
      draw_dots(ctx, mon, nx + lcd_width(num, 11, 21, 3) + 9, y + 7, s_fg, dot);
    }
    s_ghosts_on = s_ghosts;
    return;
  }
  if (seg14) {   // 14-segment LCD letters, sharp like the time
    const int h = 23, gap = 2;
    s_ghosts_on = s_ghosts;
    set_aa(s_accent);
    int nx = lcd_str14(ctx, day, x, y + 6, h, gap);
    set_aa(s_fg);
    lcd_str14(ctx, rest, nx + 2, y + 6, h, gap);
    return;
  }
  GSize wd = graphics_text_layout_get_content_size(day, s_f_date, GRect(0, 0, 200, 40), GTextOverflowModeFill, GTextAlignmentLeft);
  graphics_context_set_text_color(ctx, s_accent);
  draw_text(ctx, day, s_f_date, GRect(x, y, wd.w + 6, 32), GTextAlignmentLeft);
  graphics_context_set_text_color(ctx, s_fg);
  draw_text(ctx, rest, s_f_date, GRect(x + wd.w, y, W - x - wd.w, 32), GTextAlignmentLeft);
}

// Moon phase in the style of the Casio Tide Graph watches: a disc cut into vertical slivers by
// meridian lines. Slivers on the lit side of the terminator are filled; the rest show as unlit
// LCD segments, like the digits.
static void draw_moon(GContext *ctx, int cx, int cy, int r, time_t t) {
  const int32_t SYNODIC_S = 2551443;
  const int SLICES = 5;
  int32_t into = (int32_t)(((int64_t)t - 947182440) % SYNODIC_S);
  if (into < 0) into += SYNODIC_S;
  int32_t angle = (int32_t)((int64_t)TRIG_MAX_ANGLE * into / SYNODIC_S);
  bool waxing = into < SYNODIC_S / 2;
  for (int dy = -r; dy <= r; dy++) {
    int w = 0;
    while ((w + 1) * (w + 1) + dy * dy <= r * r) w++;
    int xt = w * cos_lookup(angle) / TRIG_MAX_RATIO;   // terminator
    int x0 = waxing ? xt : -w, x1 = waxing ? w : -xt;
    if (s_ghosts) {
      graphics_context_set_fill_color(ctx, mix(s_fg, s_bg, 28));   // 12% (s_ghost) rounds to the background on the 64-colour palette
      graphics_fill_rect(ctx, GRect(cx - w, cy + dy, 2 * w + 1, 1), 0, GCornerNone);
    }
    graphics_context_set_fill_color(ctx, s_fg);
    if (x1 >= x0) graphics_fill_rect(ctx, GRect(cx + x0, cy + dy, x1 - x0 + 1, 1), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, s_bg);
    for (int k = 1; k < SLICES; k++) {   // meridians at cos(k*pi/SLICES), both sides of the centre
      int xb = w * cos_lookup(TRIG_MAX_ANGLE * k / (2 * SLICES)) / TRIG_MAX_RATIO;
      graphics_fill_rect(ctx, GRect(cx + xb, cy + dy, 1, 1), 0, GCornerNone);
    }
  }
}

// Daylight bar: sunrise to sunset by day, sunset to sunrise by night. Lit cells are the time
// already elapsed; the current cell stands out in the foreground colour and a little taller.
// Sun path drawn like an LCD bar graph: columns of small blocks rise to an arch from sunrise to
// sunset. The whole arch stays unlit and only the current moment is highlighted; at night the highlight
// waits at the end the sun will start from.
static void draw_arc(GContext *ctx, int x0, int w, int hy, int rows, int now_min) {
  const int col = 4, row = 3;
  int len = s_sunset - s_sunrise;
  if (len < 60) len = 60;
  bool day = now_min >= s_sunrise && now_min < s_sunset;
  int cols = w / col;
  // only the current moment is lit; at night the lit column waits at the end the sun will start from
  int cur = day ? (now_min - s_sunrise) * cols / len : now_min < s_sunrise ? 0 : cols - 1;
  graphics_context_set_fill_color(ctx, mix(s_fg, s_bg, 18));
  graphics_fill_rect(ctx, GRect(x0, hy, cols * col - 1, 1), 0, GCornerNone);
  for (int c = 0; c < cols; c++) {
    int n = 1 + (rows - 1) * sin_lookup((2 * c + 1) * (TRIG_MAX_ANGLE / 2) / (2 * cols)) / TRIG_MAX_RATIO;
    GColor k = c == cur ? s_accent : mix(s_fg, s_bg, 26);
    graphics_context_set_fill_color(ctx, k);
    for (int r = 0; r < n; r++) graphics_fill_rect(ctx, GRect(x0 + c * col, hy - (r + 1) * row + 1, col - 1, row - 1), 0, GCornerNone);
  }
}

// Top block: on the left the countdown to the next sunset (by day) or sunrise (at night), on the
// right the sun's path. G.strip_base is the top of the block; layouts without room draw the arch only.
static void draw_daylight(GContext *ctx, int now_min) {
  if (!G.labels) { draw_arc(ctx, G.mx, W - 2 * G.mx, G.strip_base, 4, now_min); return; }
  const int y0 = G.strip_base;
  bool day = now_min >= s_sunrise && now_min < s_sunset;
  int mins = day ? s_sunset - now_min : now_min < s_sunrise ? s_sunrise - now_min : 1440 - now_min + s_sunrise;
  char cd[16];
  snprintf(cd, sizeof(cd), "%d:%02d", mins / 60, mins % 60);
  s_ghosts_on = false;
  graphics_context_set_text_color(ctx, mix(s_fg, s_bg, 62));
  draw_text(ctx, day ? "SUNSET IN" : "SUNRISE IN", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(G.mx - 1, y0 - 4, 90, 16), GTextAlignmentLeft);
  set_aa(s_fg);
  lcd_str(ctx, cd, G.mx, y0 + 14, 12, 18, 3);
  s_ghosts_on = s_ghosts;
  int ax = W / 2 + 8;
  draw_arc(ctx, ax, W - G.mx - ax, y0 + 31, 7, now_min);
}

static const char *wx_glyph(int code, bool night) {
  if (code <= 1)                       return night ? "\xEF\x80\xAE" : "\xEF\x80\x8D";   // f02e night clear / f00d sunny
  if (code == 2)                       return night ? "\xEF\x82\x86" : "\xEF\x80\x82";   // f086 / f002 partly cloudy
  if (code == 45 || code == 48)        return "\xEF\x80\x94";                            // f014 fog
  if (code == 3)                       return "\xEF\x80\x93";                            // f013 cloudy
  if (code >= 95)                      return "\xEF\x80\x9E";                            // f01e thunderstorm
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "\xEF\x80\x9B";    // f01b snow
  return "\xEF\x80\x99";                                                                 // f019 rain
}

// ---------------------------------------------------------------------------
// Forecast chart (bottom-right): next 12 hours as segmented LCD bars, rain chance as dots

static bool s_fahr;
// Muted Aura temperature ramp for the forecast bars; `c` is degrees Celsius.
static uint8_t s_fc_t[FC_N], s_fc_p[FC_N];   // temperature + 50, rain percent
static int s_fc_hour = -1;                   // local hour of the first sample

static void draw_forecast(GContext *ctx, struct tm *tm) {
  const int step = G.step, bw = step - 2, x0 = W - G.mx - FC_SHOW * step + 2;
  const int seg = 4, maxseg = 6, base = G.bot_y + 42;
  GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  graphics_context_set_text_color(ctx, mix(s_fg, s_bg, 55));
  draw_text(ctx, "NEXT 12H", f, GRect(x0 - 1, G.bot_y + 1, 90, 16), GTextAlignmentLeft);
  if (s_fc_hour < 0) return;
  int off = (tm->tm_hour - s_fc_hour + 24) % 24;
  if (off > FC_N - FC_SHOW) off = FC_N - FC_SHOW;
  int lo = 255, hi = 0;
  for (int i = 0; i < FC_SHOW; i++) {
    int v = s_fc_t[off + i];
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  for (int i = 0; i < FC_SHOW; i++) {
    int n = hi == lo ? 3 : 1 + (s_fc_t[off + i] - lo) * (maxseg - 1) / (hi - lo);
    int x = x0 + i * step;
    graphics_context_set_fill_color(ctx, i == 0 ? s_accent : mix(s_accent, s_bg, 62));
    for (int k = 0; k < n; k++) graphics_fill_rect(ctx, GRect(x, base - (k + 1) * seg + 1, bw, seg - 1), 0, GCornerNone);
    int pop = s_fc_p[off + i];
    if (pop >= 20) {
      graphics_context_set_fill_color(ctx, pop >= 50 ? s_fg : mix(s_fg, s_bg, 35));
      graphics_fill_rect(ctx, GRect(x + 1, base + 4, bw - 2, pop >= 50 ? 4 : 2), 0, GCornerNone);
    }
  }
  graphics_context_set_text_color(ctx, mix(s_fg, s_bg, 70));
  for (int i = 0; i < FC_SHOW; i += 4) {
    char hb[4];
    snprintf(hb, sizeof(hb), "%02d", (tm->tm_hour + i) % 24);
    draw_text(ctx, hb, f, GRect(x0 + i * step - 2, base + 8, 24, 16), GTextAlignmentLeft);
  }
}

// ---------------------------------------------------------------------------
// Main layer

// Timeline layout: date and weather on top, the time, then the sun strip and the forecast
// stacked underneath as one "day timeline".
static void paint_timeline(GContext *ctx, struct tm *tm, int now_min) {
  char buf[24];
  const int mx = G.mx;
  draw_date(ctx, tm, mx, G.date_y);
  GFont f24 = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), f18 = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  if (s_wx_ok) {
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", s_wx_temp);
    graphics_context_set_text_color(ctx, s_fg);
    draw_text(ctx, buf, f24, GRect(W - mx - 56, -2, 56, 28), GTextAlignmentRight);
    GSize ts = graphics_text_layout_get_content_size(buf, f24, GRect(0, 0, 100, 30), GTextOverflowModeFill, GTextAlignmentRight);
    graphics_context_set_text_color(ctx, s_accent);
    draw_text(ctx, wx_glyph(s_wx_code, s_night), s_f_wx, GRect(W - mx - ts.w - 36, -4, 34, 30), GTextAlignmentRight);
  }
  s_ghosts_on = s_ghosts;
  if (s_time24) snprintf(buf, sizeof(buf), "%02d:%02d", tm->tm_hour, tm->tm_min);
  else snprintf(buf, sizeof(buf), "%d:%02d", tm->tm_hour % 12 ? tm->tm_hour % 12 : 12, tm->tm_min);
  lcd_centered(ctx, buf, G.time_w, G.time_h, 5, GRect(0, G.time_y, W, G.time_h));
  draw_daylight(ctx, now_min);

  // forecast, full width
  const int step = 15, bw = 13, x0 = 10, seg = 4, maxseg = 7, base = 204;
  graphics_context_set_text_color(ctx, mix(s_fg, s_bg, 55));
  draw_text(ctx, "NEXT 12H", fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(x0 - 2, G.bot_y, 90, 16), GTextAlignmentLeft);
  if (s_wx_ok) {
    snprintf(buf, sizeof(buf), "%d / %d\xC2\xB0", s_wx_lo, s_wx_hi);
    graphics_context_set_text_color(ctx, s_fg);
    draw_text(ctx, buf, f18, GRect(W - mx - 90, G.bot_y - 4, 90, 24), GTextAlignmentRight);
  }
  if (s_fc_hour < 0) return;
  int off = (tm->tm_hour - s_fc_hour + 24) % 24;
  if (off > FC_N - FC_SHOW) off = FC_N - FC_SHOW;
  int lo = 255, hi = 0;
  for (int i = 0; i < FC_SHOW; i++) {
    int v = s_fc_t[off + i];
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  for (int i = 0; i < FC_SHOW; i++) {
    int n = hi == lo ? 3 : 1 + (s_fc_t[off + i] - lo) * (maxseg - 1) / (hi - lo);
    int x = x0 + i * step;
    graphics_context_set_fill_color(ctx, i == 0 ? s_accent : mix(s_accent, s_bg, 62));
    for (int k = 0; k < n; k++) graphics_fill_rect(ctx, GRect(x, base - (k + 1) * seg + 1, bw, seg - 1), 0, GCornerNone);
    int pop = s_fc_p[off + i];
    if (pop >= 20) {
      graphics_context_set_fill_color(ctx, pop >= 50 ? s_fg : mix(s_fg, s_bg, 35));
      graphics_fill_rect(ctx, GRect(x + 3, base + 4, bw - 6, pop >= 50 ? 4 : 2), 0, GCornerNone);
    }
  }
  graphics_context_set_text_color(ctx, mix(s_fg, s_bg, 70));
  for (int i = 0; i < FC_SHOW; i += 4) {
    snprintf(buf, sizeof(buf), "%02d", (tm->tm_hour + i) % 24);
    draw_text(ctx, buf, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD), GRect(x0 + i * step - 1, base + 8, 24, 16), GTextAlignmentLeft);
  }
}

// Stack layout: hours over minutes, huge, with a narrow column of date and weather beside them
// and the sun strip along the bottom edge.
static void paint_stack(GContext *ctx, struct tm *tm, time_t now, int now_min) {
  const int w = G.time_w, h = G.time_h, gap = 5, x0 = 6, cx = 150;
  char hh[4], mm[4], buf[16];
  if (s_time24) snprintf(hh, sizeof(hh), "%02d", tm->tm_hour);
  else snprintf(hh, sizeof(hh), "%d", tm->tm_hour % 12 ? tm->tm_hour % 12 : 12);
  snprintf(mm, sizeof(mm), "%02d", tm->tm_min);
  s_ghosts_on = s_ghosts;
  lcd_str(ctx, hh, x0 + lcd_width("88", w, h, gap) - lcd_width(hh, w, h, gap), 6, w, h, gap);
  lcd_str(ctx, mm, x0, 106, w, h, gap);

  GFont f24 = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), f18 = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  char d[4];
  for (int i = 0; i < 3; i++) d[i] = DAYS[tm->tm_wday][i] & ~0x20;
  d[3] = 0;
  graphics_context_set_text_color(ctx, s_accent);
  draw_text(ctx, d, f24, GRect(cx, -2, 50, 28), GTextAlignmentLeft);
  graphics_context_set_text_color(ctx, s_fg);
  for (int i = 0; i < 3; i++) buf[i] = MONTHS[tm->tm_mon][i] & ~0x20;
  buf[3] = 0;
  char dd[4];
  snprintf(dd, sizeof(dd), "%d", tm->tm_mday);
  draw_text(ctx, s_mdy ? buf : dd, f24, GRect(cx, 22, 50, 28), GTextAlignmentLeft);
  draw_text(ctx, s_mdy ? dd : buf, f24, GRect(cx, 46, 50, 28), GTextAlignmentLeft);
  draw_moon(ctx, cx + 20, 92, 10, now);
  if (s_wx_ok) {
    graphics_context_set_text_color(ctx, s_accent);
    draw_text(ctx, wx_glyph(s_wx_code, s_night), s_f_wx, GRect(cx, 118, 44, 34), GTextAlignmentLeft);
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", s_wx_temp);
    graphics_context_set_text_color(ctx, s_fg);
    draw_text(ctx, buf, f24, GRect(cx, 150, 50, 28), GTextAlignmentLeft);
    snprintf(buf, sizeof(buf), "%d/%d", s_wx_lo, s_wx_hi);
    graphics_context_set_text_color(ctx, mix(s_fg, s_bg, 70));
    draw_text(ctx, buf, f18, GRect(cx, 174, 50, 24), GTextAlignmentLeft);
  }
  draw_daylight(ctx, now_min);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  time_t now = time(NULL);
  struct tm *tm = localtime(&now);
  int now_min = tm->tm_hour * 60 + tm->tm_min;
  char buf[24];

  graphics_context_set_antialiased(ctx, false);
  graphics_context_set_fill_color(ctx, s_body);
  graphics_fill_rect(ctx, GRect(0, 0, W, H), 0, GCornerNone);
  s_ghosts_on = s_ghosts;
  if (s_layout == 2) { paint_timeline(ctx, tm, now_min); return; }
  if (s_layout == 3) { paint_stack(ctx, tm, now, now_min); return; }

  draw_date(ctx, tm, G.mx, G.date_y);
  draw_moon(ctx, W - G.mx - 9, G.date_y + 16, 9, now);
  draw_daylight(ctx, now_min);

  if (s_time24) snprintf(buf, sizeof(buf), "%02d:%02d", tm->tm_hour, tm->tm_min);
  else snprintf(buf, sizeof(buf), "%d:%02d", tm->tm_hour % 12 ? tm->tm_hour % 12 : 12, tm->tm_min);
  lcd_centered(ctx, buf, G.time_w, G.time_h, 5, GRect(0, G.time_y, W, G.time_h));


  // Bottom-left: weather icon, current temperature large, low / high small
  int by = G.bot_y, mx = G.mx;
  graphics_context_set_text_color(ctx, s_accent);
  if (s_wx_ok) {
    draw_text(ctx, wx_glyph(s_wx_code, s_night), s_f_wx, GRect(mx, by + 2, 40, 36), GTextAlignmentLeft);
    snprintf(buf, sizeof(buf), "%d", s_wx_temp);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  int tw = strlen(buf) > 2 ? 12 : 15;
  s_ghosts_on = s_ghosts;
  int tx = mx + 38;
  lcd_str(ctx, buf, tx, by + 4, tw, 34, 4);
  graphics_context_set_stroke_color(ctx, s_fg);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_circle(ctx, GPoint(tx + lcd_width(buf, tw, 34, 4) + 5, by + 8), 3);
  graphics_context_set_stroke_width(ctx, 1);
  if (s_wx_ok) {
    GFont f = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
    char lo[8], hi[8];
    snprintf(lo, sizeof(lo), "%d", s_wx_lo);
    snprintf(hi, sizeof(hi), "%d", s_wx_hi);
    graphics_context_set_stroke_color(ctx, s_accent);
    tri(ctx, mx, by + 47, false);
    graphics_context_set_text_color(ctx, s_fg);
    draw_text(ctx, lo, f, GRect(mx + 10, by + 36, 40, 28), GTextAlignmentLeft);
    tri(ctx, mx + 44, by + 47, true);
    draw_text(ctx, hi, f, GRect(mx + 54, by + 36, 40, 28), GTextAlignmentLeft);
  }
  draw_forecast(ctx, tm);
}

// ---------------------------------------------------------------------------
// Ticks, messages

static void refresh(void) {
  time_t now = time(NULL);
  apply_theme(localtime(&now));
  layer_mark_dirty(s_canvas);
}

static void tick_handler(struct tm *t, TimeUnits units) {
  refresh();
}

static int tuple_int(Tuple *tp) {
  return (tp->type == TUPLE_CSTRING) ? atoi(tp->value->cstring) : (int)tp->value->int32;
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *tp;
#define GET_INT(key, var, pkey) if ((tp = dict_find(iter, MESSAGE_KEY_##key))) { var = tuple_int(tp); persist_write_int(pkey, var); }
#define GET_BOOL(key, var, pkey) if ((tp = dict_find(iter, MESSAGE_KEY_##key))) { var = tuple_int(tp) != 0; persist_write_bool(pkey, var); }
  GET_INT(THEME, s_theme, PKEY_THEME)
  GET_INT(PRESET, s_preset, PKEY_PRESET)
  GET_INT(DAY_BG, s_day_bg, PKEY_DAY_BG)
  GET_INT(DAY_FG, s_day_fg, PKEY_DAY_FG)
  GET_INT(NIGHT_BG, s_night_bg, PKEY_NIGHT_BG)
  GET_INT(NIGHT_FG, s_night_fg, PKEY_NIGHT_FG)
  GET_INT(ACCENT, s_accent_pref, PKEY_ACCENT)
  GET_BOOL(TIME24, s_time24, PKEY_TIME24)
  GET_BOOL(DATE_MDY, s_mdy, PKEY_MDY)
  GET_BOOL(GHOSTS, s_ghosts, PKEY_GHOSTS)
  GET_INT(WX_TEMP, s_wx_temp, PKEY_WX_TEMP)
  GET_INT(WX_CODE, s_wx_code, PKEY_WX_CODE)
  GET_INT(WX_HI, s_wx_hi, PKEY_WX_HI)
  GET_INT(WX_LO, s_wx_lo, PKEY_WX_LO)
  GET_INT(SUNRISE, s_sunrise, PKEY_SUNRISE)
  GET_INT(SUNSET, s_sunset, PKEY_SUNSET)
  GET_BOOL(UNITS, s_fahr, PKEY_FC + 3)
  GET_INT(LAYOUT, s_layout, PKEY_LAYOUT)
  if (s_layout == 1) s_layout = 0;
  GET_INT(DATE_STYLE, s_date_style, PKEY_DATESTYLE)
  GET_INT(FC_H, s_fc_hour, PKEY_FC + 1)
  if ((tp = dict_find(iter, MESSAGE_KEY_FC_T)) && tp->length >= FC_N) { memcpy(s_fc_t, tp->value->data, FC_N); persist_write_data(PKEY_FC, s_fc_t, FC_N); }
  if ((tp = dict_find(iter, MESSAGE_KEY_FC_P)) && tp->length >= FC_N) { memcpy(s_fc_p, tp->value->data, FC_N); persist_write_data(PKEY_FC + 2, s_fc_p, FC_N); }
  if ((tp = dict_find(iter, MESSAGE_KEY_WX_OK))) { s_wx_ok = tp->value->int32 != 0; persist_write_bool(PKEY_WX_OK, s_wx_ok); }
#undef GET_INT
#undef GET_BOOL
  refresh();
}

static void load_settings(void) {
#define LOAD_INT(var, pkey) if (persist_exists(pkey)) var = persist_read_int(pkey);
#define LOAD_BOOL(var, pkey) if (persist_exists(pkey)) var = persist_read_bool(pkey);
  LOAD_INT(s_theme, PKEY_THEME)
  LOAD_INT(s_preset, PKEY_PRESET)
  LOAD_INT(s_day_bg, PKEY_DAY_BG)
  LOAD_INT(s_day_fg, PKEY_DAY_FG)
  LOAD_INT(s_night_bg, PKEY_NIGHT_BG)
  LOAD_INT(s_night_fg, PKEY_NIGHT_FG)
  LOAD_INT(s_accent_pref, PKEY_ACCENT)
  LOAD_BOOL(s_time24, PKEY_TIME24)
  LOAD_BOOL(s_mdy, PKEY_MDY)
  LOAD_BOOL(s_ghosts, PKEY_GHOSTS)
  LOAD_INT(s_wx_temp, PKEY_WX_TEMP)
  LOAD_INT(s_wx_code, PKEY_WX_CODE)
  LOAD_INT(s_wx_hi, PKEY_WX_HI)
  LOAD_INT(s_wx_lo, PKEY_WX_LO)
  LOAD_INT(s_sunrise, PKEY_SUNRISE)
  LOAD_INT(s_sunset, PKEY_SUNSET)
  LOAD_BOOL(s_wx_ok, PKEY_WX_OK)
  LOAD_INT(s_fc_hour, PKEY_FC + 1)
  LOAD_BOOL(s_fahr, PKEY_FC + 3)
  LOAD_INT(s_layout, PKEY_LAYOUT)
  LOAD_INT(s_date_style, PKEY_DATESTYLE)
  if (s_layout < 0 || s_layout > 3 || s_layout == 1) s_layout = 0;
  if (persist_exists(PKEY_FC)) persist_read_data(PKEY_FC, s_fc_t, FC_N);
  if (persist_exists(PKEY_FC + 2)) persist_read_data(PKEY_FC + 2, s_fc_p, FC_N);
#undef LOAD_INT
#undef LOAD_BOOL
}

static void window_load(Window *w) {
  Layer *root = window_get_root_layer(w);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *w) {
  layer_destroy(s_canvas);
}

static void init(void) {
  s_f_wx  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_WX_26));
  s_f_date  = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  load_settings();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  time_t now = time(NULL);
  apply_theme(localtime(&now));
  window_stack_push(s_window, true);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(256, 64);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
  fonts_unload_custom_font(s_f_wx);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
