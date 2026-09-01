#include <pebble.h>

// Aura Essential watchface
// A close homage to the "Essential" layout (Kiezel), sized natively for the
// Pebble Time 2 (200x228): a solid, configurable colour background with three
// big icon + label complications across the top, a large LECO time in a white
// band, and a weekday + date footer. Three clear horizontal areas. Each of the
// three top slots is independently configurable (Clay) across steps, heart
// rate, battery, day of week and weather. Weather arrives from the phone over
// AppMessage (Open-Meteo, no API key); everything else is standalone. Colours
// snap to the 64-colour emery palette (docs/PALETTE.md).

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

// ---- Background themes (index shared with config.js COLOR_OPTIONS) ---------
// dark_content = true means black icons/text (bright backgrounds); false means
// white content (dark backgrounds). The time always sits on a white band.
static const uint32_t THEME_HEX[7]      = { 0xFF5500, 0x00AAFF, 0x00AA00, 0xFFFF00, 0xFF0000, 0x5500AA, 0x000000 };
static const bool     THEME_DARKTEXT[7] = { true,     true,     true,     true,     false,    false,    false };

// ---- Persist keys (own numbering, distinct from the AppMessage keys) -------
#define PKEY_SLOT1   1
#define PKEY_SLOT2   2
#define PKEY_SLOT3   3
#define PKEY_BGCOLOR 4
#define PKEY_WX_TEMP 5
#define PKEY_WX_CODE 6
#define PKEY_WX_OK   7

// This SDK emits the MESSAGE_KEY_* ids into message_keys.auto.c but leaves them
// undeclared in the auto header, so declare the ones we read as extern here.
extern uint32_t MESSAGE_KEY_SLOT1;
extern uint32_t MESSAGE_KEY_SLOT2;
extern uint32_t MESSAGE_KEY_SLOT3;
extern uint32_t MESSAGE_KEY_BGCOLOR;
extern uint32_t MESSAGE_KEY_WX_TEMP;
extern uint32_t MESSAGE_KEY_WX_CODE;
extern uint32_t MESSAGE_KEY_WX_OK;

static Window *s_window;
static Layer  *s_layer;

static int  s_slot[3] = { C_STEPS, C_HEART, C_BATTERY };  // default top row
static int  s_bg      = 0;                                // theme index
static int  s_wx_temp = 0;
static int  s_wx_code = 0;
static bool s_wx_ok   = false;

static GFont s_f_time;    // big LECO time
static GFont s_f_label;   // complication labels + date
static GFont s_f_small;   // day-of-month inside the calendar glyph

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

static void icon_heart(GContext *ctx, int cx, int cy, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  graphics_fill_circle(ctx, GPoint(cx - 7, cy - 5), 9);
  graphics_fill_circle(ctx, GPoint(cx + 7, cy - 5), 9);
  GPoint tri[3] = { { cx - 15, cy - 1 }, { cx + 15, cy - 1 }, { cx, cy + 16 } };
  GPathInfo info = { .num_points = 3, .points = tri };
  GPath *p = gpath_create(&info);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

// A side-profile sneaker silhouette for the step count.
static void icon_shoe(GContext *ctx, int cx, int cy, GColor col) {
  graphics_context_set_fill_color(ctx, col);
  GPoint shoe[8] = {
    { cx - 17, cy - 3 }, { cx - 14, cy - 10 }, { cx - 4, cy - 10 }, { cx + 1, cy - 4 },
    { cx + 9, cy - 2 },  { cx + 18, cy + 3 },  { cx + 18, cy + 8 }, { cx - 17, cy + 8 },
  };
  GPathInfo info = { .num_points = 8, .points = shoe };
  GPath *p = gpath_create(&info);
  gpath_draw_filled(ctx, p);
  gpath_destroy(p);
}

// A vertical battery, filled from the bottom by pct.
static void icon_battery(GContext *ctx, int cx, int cy, GColor col, int pct, bool chg) {
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
  graphics_context_set_fill_color(ctx, (pct <= 20 && !chg) ? GColorRed : col);
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

// One top complication: a big icon (centred on icon_cy) over a bold label.
static void draw_comp(GContext *ctx, int type, int cx, int cell_w, GColor fg) {
  if (type == C_NONE) return;
  const int icon_cy = 48;
  char label[12];
  GColor lcol = fg;

  switch (type) {
    case C_STEPS: {
      int s = today_steps();
      icon_shoe(ctx, cx, icon_cy, fg);
      if (s < 0)           snprintf(label, sizeof(label), "--");
      else if (s >= 10000) snprintf(label, sizeof(label), "%dk", s / 1000);
      else                 snprintf(label, sizeof(label), "%d", s);
      break;
    }
    case C_HEART: {
      int hr = current_hr();
      icon_heart(ctx, cx, icon_cy, fg);
      if (hr < 0) snprintf(label, sizeof(label), "--");
      else        snprintf(label, sizeof(label), "%d", hr);
      break;
    }
    case C_BATTERY: {
      BatteryChargeState b = battery_state_service_peek();
      icon_battery(ctx, cx, icon_cy, fg, b.charge_percent, b.is_charging);
      snprintf(label, sizeof(label), "%d%%", b.charge_percent);
      if (b.charge_percent <= 20 && !b.is_charging) lcol = GColorRed;
      break;
    }
    case C_DAY: {
      time_t now = time(NULL);
      struct tm *t = localtime(&now);
      icon_calendar(ctx, cx, icon_cy, fg, t->tm_mday);
      strftime(label, sizeof(label), "%a", t);
      upcase(label);
      break;
    }
    case C_WEATHER: {
      icon_weather(ctx, cx, icon_cy, fg, s_wx_code, s_wx_ok);
      if (s_wx_ok) snprintf(label, sizeof(label), "%d\xC2\xB0", s_wx_temp);
      else         snprintf(label, sizeof(label), "--");
      break;
    }
    default: return;
  }

  graphics_context_set_text_color(ctx, lcol);
  graphics_draw_text(ctx, label, s_f_label, GRect(cx - cell_w / 2, 78, cell_w, 30),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

static void face_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int w = b.size.w;

  GColor bg = GColorFromHEX(THEME_HEX[s_bg]);
  GColor fg = THEME_DARKTEXT[s_bg] ? GColorBlack : GColorWhite;

  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  // Area 1: three complications across the top.
  int cw = w / 3;
  for (int i = 0; i < 3; i++) {
    draw_comp(ctx, s_slot[i], cw / 2 + i * cw, cw, fg);
  }

  // Area 2: the time in a white band, framed by separator lines.
  int band_y = 116, band_h = 60;
  graphics_context_set_fill_color(ctx, fg);
  graphics_fill_rect(ctx, GRect(0, band_y - 3, w, 3), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(0, band_y + band_h, w, 3), 0, GCornerNone);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, GRect(0, band_y, w, band_h), 0, GCornerNone);

  char tbuf[8];
  if (clock_is_24h_style()) {
    snprintf(tbuf, sizeof(tbuf), "%02d:%02d", t->tm_hour, t->tm_min);
  } else {
    int h12 = t->tm_hour % 12; if (h12 == 0) h12 = 12;
    snprintf(tbuf, sizeof(tbuf), "%d:%02d", h12, t->tm_min);
  }
  graphics_context_set_text_color(ctx, GColorBlack);
  graphics_draw_text(ctx, tbuf, s_f_time, GRect(0, band_y + 6, w, band_h - 6),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);

  // Area 3: weekday + date footer (no extra rule above it).
  char dbuf[24];
  strftime(dbuf, sizeof(dbuf), "%a %d %b", t);
  upcase(dbuf);
  graphics_context_set_text_color(ctx, fg);
  graphics_draw_text(ctx, dbuf, s_f_label, GRect(0, band_y + band_h + 8, w, 30),
                     GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// A Clay select may arrive as an int or a numeric string; accept both.
static int tuple_int(Tuple *tp) {
  return (tp->type == TUPLE_CSTRING) ? atoi(tp->value->cstring) : (int)tp->value->int32;
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *tp;
  if ((tp = dict_find(iter, MESSAGE_KEY_SLOT1)))   { s_slot[0] = tuple_int(tp); persist_write_int(PKEY_SLOT1, s_slot[0]); }
  if ((tp = dict_find(iter, MESSAGE_KEY_SLOT2)))   { s_slot[1] = tuple_int(tp); persist_write_int(PKEY_SLOT2, s_slot[1]); }
  if ((tp = dict_find(iter, MESSAGE_KEY_SLOT3)))   { s_slot[2] = tuple_int(tp); persist_write_int(PKEY_SLOT3, s_slot[2]); }
  if ((tp = dict_find(iter, MESSAGE_KEY_BGCOLOR))) { s_bg = tuple_int(tp); if (s_bg < 0 || s_bg > 6) s_bg = 0; persist_write_int(PKEY_BGCOLOR, s_bg); }
  if ((tp = dict_find(iter, MESSAGE_KEY_WX_OK)))   { s_wx_ok   = tp->value->int32; persist_write_bool(PKEY_WX_OK, s_wx_ok); }
  if ((tp = dict_find(iter, MESSAGE_KEY_WX_TEMP))) { s_wx_temp = tp->value->int32; persist_write_int(PKEY_WX_TEMP, s_wx_temp); }
  if ((tp = dict_find(iter, MESSAGE_KEY_WX_CODE))) { s_wx_code = tp->value->int32; persist_write_int(PKEY_WX_CODE, s_wx_code); }
  layer_mark_dirty(s_layer);
}

static void load_settings(void) {
  if (persist_exists(PKEY_SLOT1))   s_slot[0] = persist_read_int(PKEY_SLOT1);
  if (persist_exists(PKEY_SLOT2))   s_slot[1] = persist_read_int(PKEY_SLOT2);
  if (persist_exists(PKEY_SLOT3))   s_slot[2] = persist_read_int(PKEY_SLOT3);
  if (persist_exists(PKEY_BGCOLOR)) s_bg      = persist_read_int(PKEY_BGCOLOR);
  if (persist_exists(PKEY_WX_TEMP)) s_wx_temp = persist_read_int(PKEY_WX_TEMP);
  if (persist_exists(PKEY_WX_CODE)) s_wx_code = persist_read_int(PKEY_WX_CODE);
  if (persist_exists(PKEY_WX_OK))   s_wx_ok   = persist_read_bool(PKEY_WX_OK);
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
  s_f_time  = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  s_f_label = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
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
