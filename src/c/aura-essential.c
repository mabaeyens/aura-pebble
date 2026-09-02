#include <pebble.h>

// Aura Essential watchface
// A close homage to the "Essential" layout (Kiezel), sized natively for the
// Pebble Time 2 (200x228, 64-colour): three horizontal blocks. The top block
// carries three configurable icon + label complications (big icons, big bold
// numbers); the middle band holds a large clock; the bottom block is a clean
// empty colour strip. Everything is configurable from the phone (Clay): each of
// the three slots (steps / heart rate / battery / day of week / weather), the
// three block colours, the clock font and the separator. Content colour (black
// or white) is derived from each block's luminance so text always reads.
// Weather arrives over AppMessage (Open-Meteo, no API key); the rest is
// standalone.

// ---- Complication kinds (values shared with src/pkjs/config.js) -----------
#define C_NONE    0
#define C_STEPS   1
#define C_HEART   2
#define C_BATTERY 3
#define C_DAY     4
#define C_WEATHER 5

// ---- Weather categories (coarse WMO buckets) ------------------------------
#define WX_CLEAR  0
#define WX_PARTLY 1
#define WX_CLOUD  2
#define WX_FOG    3
#define WX_RAIN   4
#define WX_SNOW   5
#define WX_STORM  6

// ---- Palette (index shared with config.js COLOR_OPTIONS) ------------------
static const uint32_t COLOR_HEX[10] = {
  0xFF5500, 0x00AAFF, 0x00AA00, 0xFFFF00, 0xFF0000,
  0x5500AA, 0x000000, 0xFFFFFF, 0x555555, 0xAAAAAA
};

// ---- Persist keys (own numbering, distinct from the AppMessage keys) -------
#define PKEY_SLOT1     1
#define PKEY_SLOT2     2
#define PKEY_SLOT3     3
#define PKEY_WX_TEMP   5
#define PKEY_WX_CODE   6
#define PKEY_WX_OK     7
#define PKEY_TOPCOLOR  10
#define PKEY_BANDCOLOR 11
#define PKEY_TIMEFONT  12
#define PKEY_SEPCOLOR  13
#define PKEY_BOTCOLOR  14
#define PKEY_COMPCOLOR 15
#define PKEY_TIMECOLOR 16

// Pull in the MESSAGE_KEY_* ids from the generated header rather than declaring
// them extern by hand. pebble-tool 5.0.40 emits them as extern uint32_t globals
// (defined in message_keys.auto.c) while 5.0.39 (what CloudPebble runs) emits
// them as #define macros with no globals to link against; including the header
// works with either convention, hand-written externs only work with the former.
#include "message_keys.auto.h"

static Window *s_window;
static Layer  *s_layer;

static int  s_slot[3]  = { C_DAY, C_STEPS, C_BATTERY };  // default top row (Essential + one)
static int  s_top      = 0;   // top block colour index
static int  s_band     = 7;   // time band colour index (white)
static int  s_bot      = 0;   // bottom block colour index
static int  s_font     = 0;   // time font index
static int  s_sep      = 6;   // separator colour index (black); -1 = off
static int  s_comp     = 7;   // complication fill colour index (white)
static int  s_timecol  = -1;  // time digit colour index; -1 = auto by band luminance
static int  s_wx_temp  = 0;
static int  s_wx_code  = 0;
static bool s_wx_ok    = false;

static GFont s_f_num;     // numeric complication labels, LECO (segmented)
static GFont s_f_date;    // larger LECO for the date inside the calendar
static GFont s_f_day;     // weekday word, bundled LECO 1976 (letters + numbers)
static GBitmap *s_shoe_bmp;  // the Essential shoe icon, extracted bit-by-bit

static GColor palette(int i) {
  if (i < 0 || i > 9) i = 0;
  return GColorFromHEX(COLOR_HEX[i]);
}

// Black on bright backgrounds, white on dark ones. GColor8 channels are 2-bit.
static GColor content_on(GColor bg) {
  int lum = bg.r * 2 + bg.g * 3 + bg.b;   // 0..18, green weighted
  return (lum >= 9) ? GColorBlack : GColorWhite;
}

static GFont time_font(void) {
  switch (s_font) {
    case 1:  return fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49);
    case 2:  return fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
    case 3:  return fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    default:
#ifdef PBL_PLATFORM_EMERY
      return fonts_get_system_font(FONT_KEY_LECO_60_NUMBERS_AM_PM);   // Pebble Time 2
#else
      return fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);         // no 60px LECO here
#endif
  }
}

// Each font's line box pads the digits asymmetrically (leading above the ink),
// so centring by the measured content height leaves the digits sitting low.
// These offsets pull the ink back to the true vertical centre; LECO 60 was
// measured 9px low on emery, the others are near-centred.
static int time_voffset(void) {
  switch (s_font) {
    case 1:  return -3;   // Roboto 49
    case 2:  return -2;   // Bitham 42
    case 3:  return  0;   // Gothic 28
    default: return -9;   // LECO
  }
}

static void upcase(char *s) {
  for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32;
}

static int today_steps(void) {
#if defined(PBL_HEALTH)
  HealthServiceAccessibilityMask m =
      health_service_metric_accessible(HealthMetricStepCount, time_start_of_today(), time(NULL));
  if (m & HealthServiceAccessibilityMaskAvailable)
    return (int)health_service_sum_today(HealthMetricStepCount);
#endif
  return -1;
}

static int current_hr(void) {
#if defined(PBL_HEALTH)
  HealthValue v = health_service_peek_current_value(HealthMetricHeartRateBPM);
  if (v > 0) return (int)v;
#endif
  return -1;
}

static int wx_category(int code) {
  if (code == 0) return WX_CLEAR;
  if (code <= 2) return WX_PARTLY;         // 1 mainly clear, 2 partly cloudy: sun + cloud
  if (code == 3) return WX_CLOUD;          // 3 overcast: plain cloud, no sun
  if (code == 45 || code == 48) return WX_FOG;
  if (code >= 71 && code <= 77) return WX_SNOW;
  if (code >= 85 && code <= 86) return WX_SNOW;
  if (code >= 95) return WX_STORM;
  if (code >= 51) return WX_RAIN;   // 51-67 drizzle/rain, 80-82 showers
  return WX_CLOUD;
}

// ---- Complication icons (drawn in code, ~40px, so the face carries no PNGs) -

#define ICON_CY 49   // vertical centre of the complication icon
#define LABEL_Y 81   // top of the complication label

static void icon_heart(GContext *ctx, int cx, int cy, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_circle(ctx, GPoint(cx - 10, cy - 7), 11);
  graphics_fill_circle(ctx, GPoint(cx + 10, cy - 7), 11);
  GPoint tri[3] = { { cx - 20, cy - 1 }, { cx + 20, cy - 1 }, { cx, cy + 22 } };
  GPathInfo info = { .num_points = 3, .points = tri };
  GPath *p = gpath_create(&info);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

// A vertical battery, filled from the bottom by pct.
static void icon_battery(GContext *ctx, int cx, int cy, GColor col, int pct) {
  int w = 24, h = 36;   // sized to sit level with the other complication icons
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  GRect body = GRect(cx - w / 2, cy - h / 2 + 2, w, h);   // +2 to leave room for the nub above
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_rect(ctx, GRect(cx - 6, body.origin.y - 5, 12, 6), 1, GCornersTop);  // nub
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_rect(ctx, body);
  int inner = h - 8;
  int fill = inner * pct / 100;   // charge level: fills from the bottom, empties toward it
  graphics_fill_rect(ctx, GRect(body.origin.x + 4, body.origin.y + 4 + (inner - fill), w - 8, fill),
                     0, GCornerNone);
}

// The white calendar body only; the black outline comes from the draw_comp
// halo, and the black binder tabs and day number are added by calendar_detail.
static void icon_calendar(GContext *ctx, int cx, int cy, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_rect(ctx, GRect(cx - 18, cy - 11, 36, 33), 4, GCornersAll);   // rounded body
}

// Black detail over the white calendar body: two binder tabs sticking up and
// the day-of-month in the segmented font, both black as on the original.
static void calendar_detail(GContext *ctx, int cx, int cy, int mday) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(cx - 11, cy - 20, 5, 12), 2, GCornersTop);   // binder tabs
  graphics_fill_rect(ctx, GRect(cx + 6,  cy - 20, 5, 12), 2, GCornersTop);
  char d[3];
  snprintf(d, sizeof(d), "%d", mday);
  graphics_context_set_text_color(ctx, GColorBlack);
  // Vertically centre the date in the readable white area. The body runs
  // cy-11..cy+22; the tabs cover the very top, so the visible white centres a
  // little below the body midpoint. Measure the glyph and place its box so the
  // digit sits on that centre (LECO's tall line box otherwise bottom-aligns it).
  GRect box = GRect(cx - 18, cy - 3, 36, 28);
  GSize sz = graphics_text_layout_get_content_size(
      d, s_f_date, box, GTextOverflowModeFill, GTextAlignmentCenter);
  box.origin.y = (cy + 3) - sz.h / 2;   // +3 (not the body midpoint): LECO sits low in its line box
  graphics_draw_text(ctx, d, s_f_date, box,
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// A puffy cloud: lumpy top lobes and a scalloped (rounded) bottom made of a row
// of overlapping bumps, so the base reads as cloud bumps rather than a flat bar.
// Only the silhouette matters (draw_comp adds the black halo, then fills once).
static void wx_cloud(GContext *ctx, int cx, int cy, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  // Bottom bumps: three overlapping discs on a baseline -> two valleys between them.
  graphics_fill_circle(ctx, GPoint(cx - 11, cy + 3), 7);
  graphics_fill_circle(ctx, GPoint(cx,      cy + 4), 7);
  graphics_fill_circle(ctx, GPoint(cx + 11, cy + 3), 7);
  // Top lobes: a big centre puff and a smaller shoulder for a lumpy crown.
  graphics_fill_circle(ctx, GPoint(cx - 5, cy - 5), 10);
  graphics_fill_circle(ctx, GPoint(cx + 8, cy - 2), 8);
  // Body: bridge the lobes to the bumps without reaching the scalloped base.
  graphics_fill_rect(ctx, GRect(cx - 16, cy - 3, 32, 7), 0, GCornerNone);
}

static void wx_sun(GContext *ctx, int cx, int cy, GColor col, int r) {
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_circle(ctx, GPoint(cx, cy), r);
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, 3);   // 3px rays, matching every other icon stroke
  for (int i = 0; i < 8; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / 8;
    int s = sin_lookup(a), c = cos_lookup(a);
    GPoint p1 = { cx + (r + 3) * s / TRIG_MAX_RATIO, cy - (r + 3) * c / TRIG_MAX_RATIO };
    GPoint p2 = { cx + (r + 8) * s / TRIG_MAX_RATIO, cy - (r + 8) * c / TRIG_MAX_RATIO };
    graphics_draw_line(ctx, p1, p2);
  }
}

// The sun drawn on top of the cloud. In the fill pass it gets its own 3px black
// halo -- the same disc the outer halo uses -- so its ring over the cloud matches
// its outward outline and it reads as genuinely sitting on top of the cloud. In
// the outer-halo passes (fill == black) that local halo is skipped so it doesn't
// compound with the outer halo into a thicker outward outline.
static void wx_sun_outlined(GContext *ctx, int cx, int cy, GColor fill, int r) {
  if (!gcolor_equal(fill, GColorBlack)) {
    for (int dy = -3; dy <= 3; dy++)
      for (int dx = -3; dx <= 3; dx++) {
        if (dx * dx + dy * dy > 10) continue;
        wx_sun(ctx, cx + dx, cy + dy, GColorBlack, r);
      }
  }
  wx_sun(ctx, cx, cy, fill, r);
}

static void icon_weather(GContext *ctx, int cx, int cy, GColor col, int code, bool ok) {
  if (!ok) { wx_cloud(ctx, cx, cy, col); return; }
  switch (wx_category(code)) {
    case WX_CLEAR:  wx_sun(ctx, cx, cy, col, 11); break;
    // Cloud first, then the sun on top so the sun is completely visible; the sun
    // carries its own black outline (wx_sun_outlined) to separate it from the cloud.
    case WX_PARTLY: wx_cloud(ctx, cx + 4, cy + 5, col); wx_sun_outlined(ctx, cx - 8, cy - 8, col, 7); break;
    case WX_CLOUD:  wx_cloud(ctx, cx, cy, col); break;
    case WX_FOG:
      graphics_context_set_stroke_color(ctx, col);
      graphics_context_set_stroke_width(ctx, 3);
      for (int i = 0; i < 4; i++)
        graphics_draw_line(ctx, GPoint(cx - 17, cy - 10 + i * 7), GPoint(cx + 17, cy - 10 + i * 7));
      break;
    case WX_RAIN:
      wx_cloud(ctx, cx, cy - 5, col);
      graphics_context_set_stroke_color(ctx, col);
      graphics_context_set_stroke_width(ctx, 3);
      for (int i = 0; i < 3; i++)
        graphics_draw_line(ctx, GPoint(cx - 11 + i * 11, cy + 13), GPoint(cx - 11 + i * 11, cy + 22));
      break;
    case WX_SNOW:
      wx_cloud(ctx, cx, cy - 5, col);
      graphics_context_set_fill_color(ctx, col);
      for (int i = 0; i < 3; i++)
        graphics_fill_circle(ctx, GPoint(cx - 11 + i * 11, cy + 17), 3);
      break;
    case WX_STORM: {
      wx_cloud(ctx, cx, cy - 5, col);
      graphics_context_set_fill_color(ctx, col);
      GPoint bolt[4] = { { cx + 3, cy + 8 }, { cx - 7, cy + 20 }, { cx + 1, cy + 20 }, { cx - 4, cy + 30 } };
      GPathInfo info = { .num_points = 4, .points = bolt };
      GPath *p = gpath_create(&info);
      gpath_draw_filled(ctx, p);
      gpath_destroy(p);
      break;
    }
  }
}

// Draw one complication's ICON at a pixel offset in one colour. Called many
// times to build the black outline, then once more for the fill on top.
static void render_icon(GContext *ctx, int type, int cx, int dx, int dy, GColor col) {
  int icx = cx + dx, icy = ICON_CY + dy;
  switch (type) {
    case C_HEART:   icon_heart(ctx, icx, icy, col); break;
    case C_BATTERY: {
      BatteryChargeState b = battery_state_service_peek();
      icon_battery(ctx, icx, icy, col, b.charge_percent);
      break;
    }
    case C_DAY: icon_calendar(ctx, icx, icy, col); break;
    case C_WEATHER: icon_weather(ctx, icx, icy, col, s_wx_code, s_wx_ok); break;
    default: break;
  }
}

// Draw one complication's LABEL, centred under the icon. Flat, no outline, like
// the original Essential where the labels are plain white text.
static void render_label(GContext *ctx, int type, int cx, int cell_w, GColor col) {
  char label[12] = "";
  GFont f = s_f_num;   // LECO segmented digits for everything numeric
  switch (type) {
    case C_STEPS: {
      int s = today_steps();
      if (s >= 0) snprintf(label, sizeof(label), "%d", s);   // LECO has no 'K'
      break;
    }
    case C_HEART: {
      int hr = current_hr();
      if (hr > 0) snprintf(label, sizeof(label), "%d", hr);
      break;
    }
    case C_BATTERY: {
      BatteryChargeState b = battery_state_service_peek();
      snprintf(label, sizeof(label), "%d", b.charge_percent);   // LECO has no '%'
      break;
    }
    case C_DAY: {
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      strftime(label, sizeof(label), "%a", t);
      upcase(label);
      f = s_f_day;   // weekday word in bundled LECO, on par with the numeric labels
      break;
    }
    case C_WEATHER: {
      if (s_wx_ok) snprintf(label, sizeof(label), "%d", s_wx_temp);   // LECO has no degree
      break;
    }
    default: return;
  }
  if (!label[0]) return;   // nothing to show (e.g. no health/weather data)
  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, label, f, GRect(cx - cell_w / 2, LABEL_Y, cell_w, 32),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// The icon gets a thick, rounded black outline (a ~3px halo) so it reads on any
// coloured top block; the fill goes on top. The label is drawn flat underneath,
// no outline, in the complication colour (white by default).
static void draw_comp(GContext *ctx, int type, int cx, int cell_w) {
  if (type == C_NONE) return;
  if (type == C_STEPS && s_shoe_bmp) {
    // The shoe is a bit-by-bit bitmap copy of the original Essential icon; it
    // already carries its own black outline, so it needs no vector halo.
    GRect r = gbitmap_get_bounds(s_shoe_bmp);
    GRect dst = GRect(cx - r.size.w / 2, ICON_CY - r.size.h / 2, r.size.w, r.size.h);
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    graphics_draw_bitmap_in_rect(ctx, s_shoe_bmp, dst);
  } else {
    for (int dy = -3; dy <= 3; dy++) {
      for (int dx = -3; dx <= 3; dx++) {
        if (dx == 0 && dy == 0) continue;
        if (dx * dx + dy * dy > 10) continue;   // clip the halo to a ~3px disc
        render_icon(ctx, type, cx, dx, dy, GColorBlack);
      }
    }
    render_icon(ctx, type, cx, 0, 0, palette(s_comp));  // fill on top of the outline
    if (type == C_DAY) {
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      calendar_detail(ctx, cx, ICON_CY, t->tm_mday);    // black tabs + day number
    }
  }
  render_label(ctx, type, cx, cell_w, palette(s_comp)); // flat label, no outline
}

static void face_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int w = b.size.w, h = b.size.h;

  GColor top_bg  = palette(s_top);
  GColor band_bg = palette(s_band);
  GColor bot_bg  = palette(s_bot);
  GColor band_fg = (s_timecol >= 0) ? palette(s_timecol) : content_on(band_bg);

  int band_y = h * 57 / 100;   // top block ~57%, matching the original Essential
  int bot_h  = h * 11 / 100;   // short colour strip at the bottom (~11% as in the original)
  int bot_y  = h - bot_h;
  int band_h = bot_y - band_y; // time band gets the middle ~30%

  // Top block: solid colour + three complications.
  graphics_context_set_fill_color(ctx, top_bg);
  graphics_fill_rect(ctx, GRect(0, 0, w, band_y), 0, GCornerNone);
  int cw = w / 3;
  for (int i = 0; i < 3; i++) {
    draw_comp(ctx, s_slot[i], cw / 2 + i * cw, cw);
  }

  // Middle time band.
  graphics_context_set_fill_color(ctx, band_bg);
  graphics_fill_rect(ctx, GRect(0, band_y, w, band_h), 0, GCornerNone);

  // Bottom block: an empty colour strip.
  graphics_context_set_fill_color(ctx, bot_bg);
  graphics_fill_rect(ctx, GRect(0, bot_y, w, h - bot_y), 0, GCornerNone);

  // Separators bounding the time band.
  if (s_sep >= 0) {
    graphics_context_set_fill_color(ctx, palette(s_sep));
    graphics_fill_rect(ctx, GRect(0, band_y - 3, w, 6), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(0, bot_y - 3,  w, 6), 0, GCornerNone);
  }

  // The time, vertically centred in the band by its measured height.
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char tbuf[8];
  if (clock_is_24h_style()) {
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d", t->tm_hour, t->tm_min);
  } else {
    int h12 = t->tm_hour % 12; if (h12 == 0) h12 = 12;
    snprintf(tbuf, sizeof(tbuf), "%d:%02d", h12, t->tm_min);
  }
  GFont tf = time_font();
  GRect band = GRect(0, band_y, w, band_h);
  GSize sz = graphics_text_layout_get_content_size(tbuf, tf, band, GTextOverflowModeFill, GTextAlignmentCenter);
  int ty = band_y + (band_h - sz.h) / 2 + time_voffset();
  graphics_context_set_text_color(ctx, band_fg);
  graphics_draw_text(ctx, tbuf, tf, GRect(0, ty, w, sz.h + 8),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// A Clay select may arrive as an int or a numeric string; accept both.
static int tuple_int(Tuple *tp) {
  return (tp->type == TUPLE_CSTRING) ? atoi(tp->value->cstring) : (int)tp->value->int32;
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *tp;
  if ((tp = dict_find(iter, MESSAGE_KEY_SLOT1)))     { s_slot[0] = tuple_int(tp); persist_write_int(PKEY_SLOT1, s_slot[0]); }
  if ((tp = dict_find(iter, MESSAGE_KEY_SLOT2)))     { s_slot[1] = tuple_int(tp); persist_write_int(PKEY_SLOT2, s_slot[1]); }
  if ((tp = dict_find(iter, MESSAGE_KEY_SLOT3)))     { s_slot[2] = tuple_int(tp); persist_write_int(PKEY_SLOT3, s_slot[2]); }
  if ((tp = dict_find(iter, MESSAGE_KEY_TOPCOLOR)))  { s_top  = tuple_int(tp); persist_write_int(PKEY_TOPCOLOR, s_top); }
  if ((tp = dict_find(iter, MESSAGE_KEY_BANDCOLOR))) { s_band = tuple_int(tp); persist_write_int(PKEY_BANDCOLOR, s_band); }
  if ((tp = dict_find(iter, MESSAGE_KEY_BOTCOLOR)))  { s_bot  = tuple_int(tp); persist_write_int(PKEY_BOTCOLOR, s_bot); }
  if ((tp = dict_find(iter, MESSAGE_KEY_TIMEFONT)))  { s_font = tuple_int(tp); persist_write_int(PKEY_TIMEFONT, s_font); }
  if ((tp = dict_find(iter, MESSAGE_KEY_SEPCOLOR)))  { s_sep  = tuple_int(tp); persist_write_int(PKEY_SEPCOLOR, s_sep); }
  if ((tp = dict_find(iter, MESSAGE_KEY_COMPCOLOR))) { s_comp    = tuple_int(tp); persist_write_int(PKEY_COMPCOLOR, s_comp); }
  if ((tp = dict_find(iter, MESSAGE_KEY_TIMECOLOR))) { s_timecol = tuple_int(tp); persist_write_int(PKEY_TIMECOLOR, s_timecol); }
  if ((tp = dict_find(iter, MESSAGE_KEY_WX_OK)))     { s_wx_ok   = tp->value->int32; persist_write_bool(PKEY_WX_OK, s_wx_ok); }
  if ((tp = dict_find(iter, MESSAGE_KEY_WX_TEMP)))   { s_wx_temp = tp->value->int32; persist_write_int(PKEY_WX_TEMP, s_wx_temp); }
  if ((tp = dict_find(iter, MESSAGE_KEY_WX_CODE)))   { s_wx_code = tp->value->int32; persist_write_int(PKEY_WX_CODE, s_wx_code); }
  layer_mark_dirty(s_layer);
}

static void load_settings(void) {
  if (persist_exists(PKEY_SLOT1))     s_slot[0] = persist_read_int(PKEY_SLOT1);
  if (persist_exists(PKEY_SLOT2))     s_slot[1] = persist_read_int(PKEY_SLOT2);
  if (persist_exists(PKEY_SLOT3))     s_slot[2] = persist_read_int(PKEY_SLOT3);
  if (persist_exists(PKEY_TOPCOLOR))  s_top     = persist_read_int(PKEY_TOPCOLOR);
  if (persist_exists(PKEY_BANDCOLOR)) s_band    = persist_read_int(PKEY_BANDCOLOR);
  if (persist_exists(PKEY_BOTCOLOR))  s_bot     = persist_read_int(PKEY_BOTCOLOR);
  if (persist_exists(PKEY_TIMEFONT))  s_font    = persist_read_int(PKEY_TIMEFONT);
  if (persist_exists(PKEY_SEPCOLOR))  s_sep     = persist_read_int(PKEY_SEPCOLOR);
  if (persist_exists(PKEY_COMPCOLOR)) s_comp    = persist_read_int(PKEY_COMPCOLOR);
  if (persist_exists(PKEY_TIMECOLOR)) s_timecol = persist_read_int(PKEY_TIMECOLOR);
  if (persist_exists(PKEY_WX_TEMP))   s_wx_temp = persist_read_int(PKEY_WX_TEMP);
  if (persist_exists(PKEY_WX_CODE))   s_wx_code = persist_read_int(PKEY_WX_CODE);
  if (persist_exists(PKEY_WX_OK))     s_wx_ok   = persist_read_bool(PKEY_WX_OK);
}

static void tick_handler(struct tm *t, TimeUnits units) { layer_mark_dirty(s_layer); }
static void battery_handler(BatteryChargeState state)   { layer_mark_dirty(s_layer); }

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_layer, face_update_proc);
  layer_add_child(root, s_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_layer);
}

static void init(void) {
  s_f_num  = fonts_get_system_font(FONT_KEY_LECO_20_BOLD_NUMBERS);
  s_f_date = fonts_get_system_font(FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM);
  s_f_day  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_LECO_20));
  s_shoe_bmp = gbitmap_create_with_resource(RESOURCE_ID_IMG_SHOE);

  load_settings();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  if (s_shoe_bmp) gbitmap_destroy(s_shoe_bmp);
  if (s_f_day) fonts_unload_custom_font(s_f_day);
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
