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

// This SDK emits the MESSAGE_KEY_* ids into message_keys.auto.c but leaves them
// undeclared in the auto header, so declare the ones we read as extern here.
extern uint32_t MESSAGE_KEY_SLOT1;
extern uint32_t MESSAGE_KEY_SLOT2;
extern uint32_t MESSAGE_KEY_SLOT3;
extern uint32_t MESSAGE_KEY_TOPCOLOR;
extern uint32_t MESSAGE_KEY_BANDCOLOR;
extern uint32_t MESSAGE_KEY_BOTCOLOR;
extern uint32_t MESSAGE_KEY_TIMEFONT;
extern uint32_t MESSAGE_KEY_SEPCOLOR;
extern uint32_t MESSAGE_KEY_WX_TEMP;
extern uint32_t MESSAGE_KEY_WX_CODE;
extern uint32_t MESSAGE_KEY_WX_OK;

static Window *s_window;
static Layer  *s_layer;

static int  s_slot[3]  = { C_STEPS, C_HEART, C_BATTERY };  // default top row
static int  s_top      = 0;   // top block colour index
static int  s_band     = 7;   // time band colour index (white)
static int  s_bot      = 0;   // bottom block colour index
static int  s_font     = 0;   // time font index
static int  s_sep      = 6;   // separator colour index (black); -1 = off
static int  s_wx_temp  = 0;
static int  s_wx_code  = 0;
static bool s_wx_ok    = false;

static GFont s_f_label;   // complication labels (big bold)
static GFont s_f_small;   // day-of-month inside the calendar glyph

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
  if (code <= 3) return WX_PARTLY;
  if (code == 45 || code == 48) return WX_FOG;
  if (code >= 71 && code <= 77) return WX_SNOW;
  if (code >= 85 && code <= 86) return WX_SNOW;
  if (code >= 95) return WX_STORM;
  if (code >= 51) return WX_RAIN;   // 51-67 drizzle/rain, 80-82 showers
  return WX_CLOUD;
}

// ---- Complication icons (drawn in code, ~40px, so the face carries no PNGs) -

#define ICON_CY 42   // vertical centre of the complication icon
#define LABEL_Y 62   // top of the complication label

static void icon_heart(GContext *ctx, int cx, int cy, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_circle(ctx, GPoint(cx - 7, cy - 5), 8);
  graphics_fill_circle(ctx, GPoint(cx + 7, cy - 5), 8);
  GPoint tri[3] = { { cx - 14, cy - 1 }, { cx + 14, cy - 1 }, { cx, cy + 16 } };
  GPathInfo info = { .num_points = 3, .points = tri };
  GPath *p = gpath_create(&info);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

// A side-profile running shoe (toe pointing right) for the step count. The
// filled silhouette is drawn in the pass colour so the white-fill/black-outline
// treatment applies; the interior lines (sole, swoosh, laces) are added
// afterwards by shoe_detail() in black, on top of the white fill.
static void icon_shoe(GContext *ctx, int cx, int cy, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  GPoint shoe[10] = {
    { cx - 17, cy + 5 },   // heel, bottom
    { cx - 16, cy - 1 },   // heel, back
    { cx - 12, cy - 6 },   // collar, back
    { cx - 7,  cy - 7 },   // collar, front
    { cx - 5,  cy - 3 },   // tongue dip
    { cx - 1,  cy - 5 },   // instep
    { cx + 7,  cy - 2 },   // vamp
    { cx + 16, cy - 0 },   // toe tip
    { cx + 18, cy + 3 },   // toe, bottom
    { cx + 18, cy + 5 },   // sole, front (bottom edge closes back to the heel)
  };
  GPathInfo info = { .num_points = 10, .points = shoe };
  GPath *p = gpath_create(&info);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

// Interior detail for the shoe, drawn in black over the white fill.
static void shoe_detail(GContext *ctx, int cx, int cy) {
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, GPoint(cx - 16, cy + 2), GPoint(cx + 18, cy + 2));  // sole line
  graphics_draw_line(ctx, GPoint(cx - 3,  cy - 1), GPoint(cx + 9,  cy + 1));  // swoosh, low sweep
  graphics_draw_line(ctx, GPoint(cx + 9,  cy + 1), GPoint(cx + 14, cy - 3));  // swoosh, up flick
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_line(ctx, GPoint(cx - 9, cy - 4), GPoint(cx - 6, cy - 5));    // lace
  graphics_draw_line(ctx, GPoint(cx - 8, cy - 2), GPoint(cx - 5, cy - 3));    // lace
}

// A vertical battery, filled from the bottom by pct.
static void icon_battery(GContext *ctx, int cx, int cy, GColor col, int pct) {
  int w = 20, h = 32;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  GRect body = GRect(cx - w / 2, cy - h / 2, w, h);
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_rect(ctx, GRect(cx - 5, body.origin.y - 4, 10, 5), 1, GCornersTop);  // nub
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_rect(ctx, body);
  int inner = h - 8;
  int fill = inner * pct / 100;
  graphics_fill_rect(ctx, GRect(body.origin.x + 4, body.origin.y + 4 + (inner - fill), w - 8, fill),
                     0, GCornerNone);
}

static void icon_calendar(GContext *ctx, int cx, int cy, GColor col, int mday) {
  GRect body = GRect(cx - 16, cy - 11, 32, 30);
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_rect(ctx, GRect(cx - 10, cy - 16, 4, 7), 1, GCornersAll);   // binder tabs
  graphics_fill_rect(ctx, GRect(cx + 6,  cy - 16, 4, 7), 1, GCornersAll);
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, 3);
  graphics_draw_round_rect(ctx, body, 4);
  graphics_fill_rect(ctx, GRect(body.origin.x, body.origin.y, body.size.w, 7), 0, GCornerNone);
  char d[3];
  snprintf(d, sizeof(d), "%d", mday);
  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, d, s_f_small, GRect(body.origin.x, body.origin.y + 7, body.size.w, 22),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void wx_cloud(GContext *ctx, int cx, int cy, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_circle(ctx, GPoint(cx - 9, cy + 3), 7);
  graphics_fill_circle(ctx, GPoint(cx + 9, cy + 3), 7);
  graphics_fill_circle(ctx, GPoint(cx,     cy - 4), 10);
  graphics_fill_rect(ctx, GRect(cx - 16, cy + 3, 32, 8), 0, GCornerNone);
}

static void wx_sun(GContext *ctx, int cx, int cy, GColor col, int r) {
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_circle(ctx, GPoint(cx, cy), r);
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, 2);
  for (int i = 0; i < 8; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / 8;
    int s = sin_lookup(a), c = cos_lookup(a);
    GPoint p1 = { cx + (r + 3) * s / TRIG_MAX_RATIO, cy - (r + 3) * c / TRIG_MAX_RATIO };
    GPoint p2 = { cx + (r + 8) * s / TRIG_MAX_RATIO, cy - (r + 8) * c / TRIG_MAX_RATIO };
    graphics_draw_line(ctx, p1, p2);
  }
}

static void icon_weather(GContext *ctx, int cx, int cy, GColor col, int code, bool ok) {
  if (!ok) { wx_cloud(ctx, cx, cy, col); return; }
  switch (wx_category(code)) {
    case WX_CLEAR:  wx_sun(ctx, cx, cy, col, 10); break;
    case WX_PARTLY: wx_sun(ctx, cx - 8, cy - 8, col, 6); wx_cloud(ctx, cx + 4, cy + 4, col); break;
    case WX_CLOUD:  wx_cloud(ctx, cx, cy, col); break;
    case WX_FOG:
      graphics_context_set_stroke_color(ctx, col);
      graphics_context_set_stroke_width(ctx, 3);
      for (int i = 0; i < 4; i++)
        graphics_draw_line(ctx, GPoint(cx - 15, cy - 9 + i * 6), GPoint(cx + 15, cy - 9 + i * 6));
      break;
    case WX_RAIN:
      wx_cloud(ctx, cx, cy - 4, col);
      graphics_context_set_stroke_color(ctx, col);
      graphics_context_set_stroke_width(ctx, 3);
      for (int i = 0; i < 3; i++)
        graphics_draw_line(ctx, GPoint(cx - 10 + i * 10, cy + 11), GPoint(cx - 10 + i * 10, cy + 19));
      break;
    case WX_SNOW:
      wx_cloud(ctx, cx, cy - 4, col);
      graphics_context_set_fill_color(ctx, col);
      for (int i = 0; i < 3; i++)
        graphics_fill_circle(ctx, GPoint(cx - 10 + i * 10, cy + 15), 3);
      break;
    case WX_STORM: {
      wx_cloud(ctx, cx, cy - 4, col);
      graphics_context_set_fill_color(ctx, col);
      GPoint bolt[4] = { { cx + 3, cy + 7 }, { cx - 6, cy + 18 }, { cx + 1, cy + 18 }, { cx - 4, cy + 27 } };
      GPathInfo info = { .num_points = 4, .points = bolt };
      GPath *p = gpath_create(&info);
      gpath_draw_filled(ctx, p);
      gpath_destroy(p);
      break;
    }
  }
}

// Draw one complication (icon + label) at a pixel offset, all in one colour.
// Called many times to build the outline: eight black passes at 1px offsets,
// then one white pass on top, giving white fill with a black outline like the
// original Essential.
static void render_comp(GContext *ctx, int type, int cx, int cell_w, int dx, int dy, GColor col) {
  const int icon_cy = ICON_CY;
  const int label_y = LABEL_Y;
  int icx = cx + dx, icy = icon_cy + dy;
  char label[12];

  switch (type) {
    case C_STEPS: {
      int s = today_steps();
      icon_shoe(ctx, icx, icy, col);
      if (s < 0)           snprintf(label, sizeof(label), "--");
      else if (s >= 10000) snprintf(label, sizeof(label), "%dk", s / 1000);
      else                 snprintf(label, sizeof(label), "%d", s);
      break;
    }
    case C_HEART: {
      int hr = current_hr();
      icon_heart(ctx, icx, icy, col);
      if (hr < 0) snprintf(label, sizeof(label), "--");
      else        snprintf(label, sizeof(label), "%d", hr);
      break;
    }
    case C_BATTERY: {
      BatteryChargeState b = battery_state_service_peek();
      icon_battery(ctx, icx, icy, col, b.charge_percent);
      snprintf(label, sizeof(label), "%d%%", b.charge_percent);
      break;
    }
    case C_DAY: {
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      icon_calendar(ctx, icx, icy, col, t->tm_mday);
      strftime(label, sizeof(label), "%a", t);
      upcase(label);
      break;
    }
    case C_WEATHER: {
      icon_weather(ctx, icx, icy, col, s_wx_code, s_wx_ok);
      if (s_wx_ok) snprintf(label, sizeof(label), "%d\xC2\xB0", s_wx_temp);
      else         snprintf(label, sizeof(label), "--");
      break;
    }
    default: return;
  }

  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, label, s_f_label, GRect(cx - cell_w / 2 + dx, label_y + dy, cell_w, 32),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// White fill with a black outline: a black halo in the eight neighbouring
// directions, then the white fill on top.
static void draw_comp(GContext *ctx, int type, int cx, int cell_w) {
  if (type == C_NONE) return;
  static const int ox[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
  static const int oy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
  for (int k = 0; k < 8; k++) render_comp(ctx, type, cx, cell_w, ox[k], oy[k], GColorBlack);
  render_comp(ctx, type, cx, cell_w, 0, 0, GColorWhite);
  if (type == C_STEPS) shoe_detail(ctx, cx, ICON_CY);  // black interior lines on top
}

static void face_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int w = b.size.w, h = b.size.h;

  GColor top_bg  = palette(s_top);
  GColor band_bg = palette(s_band);
  GColor bot_bg  = palette(s_bot);
  GColor band_fg = content_on(band_bg);

  int band_y = h * 44 / 100;   // top block ~44%
  int bot_h  = h * 10 / 100;   // short empty strip at the bottom
  int bot_y  = h - bot_h;
  int band_h = bot_y - band_y; // time band gets the rest

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
    graphics_fill_rect(ctx, GRect(0, band_y - 2, w, 4), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(0, bot_y - 2,  w, 4), 0, GCornerNone);
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
  s_f_label = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  s_f_small = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

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
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
