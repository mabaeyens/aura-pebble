#include <pebble.h>

// Aura — analog watchface (Phase 2+): the stop-to-go railway face with dials.
//
// A Swiss-railway-style clock (see the Design-origin note in README.md — a close
// homage to the Mondaine railway clock, a registered design). White or black dial
// (settings), black/white baton hands, a red second hand with the red lollipop
// disc, and three chronograph-style subdials:
//   * LEFT (9)   weather — temperature + a condition glyph, fed from the phone
//                (PebbleKit JS -> Open-Meteo) over AppMessage.
//   * RIGHT (3)  day, as "WWW DD" (e.g. MON 01).
//   * BOTTOM (6) step count, or heart rate (settings).
// Plus an original "AURA" wordmark. All configured through a Clay settings page.
//
// The stop2go seconds mechanic (per-minute cycle, `sec` = seconds+ms in the
// minute): sweep a full 360 deg over 58 s, then hold at 12 for ~2 s; at :00 the
// minute hand jumps and the second hand releases — continuous, no snap.

#define SWEEP_SECONDS 58.0f
#define FRAME_MS 50
#define PAUSE_POLL_MS 200

// Persist keys (distinct from the AppMessage keys generated in message_keys.auto.h).
#define PKEY_SECONDS  1
#define PKEY_THEME    2
#define PKEY_WORDMARK 3
#define PKEY_BOTTOM   4
#define PKEY_WX_TEMP  5
#define PKEY_WX_CODE  6
#define PKEY_WX_OK    7

// AppMessage keys are emitted as runtime uint32_t vars in message_keys.auto.c,
// but this SDK's generated header leaves them undeclared — declare them here.
extern uint32_t MESSAGE_KEY_THEME;
extern uint32_t MESSAGE_KEY_SECONDS;
extern uint32_t MESSAGE_KEY_WORDMARK;
extern uint32_t MESSAGE_KEY_BOTTOM;
extern uint32_t MESSAGE_KEY_WX_TEMP;
extern uint32_t MESSAGE_KEY_WX_CODE;
extern uint32_t MESSAGE_KEY_WX_OK;

static Window *s_window;
static Layer *s_face_layer;
static AppTimer *s_timer;

// Settings.
static bool s_seconds = true;
static bool s_theme_dark = false;
static bool s_wordmark = true;
static bool s_bottom_heart = false;   // false = steps, true = heart rate

// Weather (pushed from the phone).
static int s_wx_temp = 0;
static int s_wx_code = 0;
static bool s_wx_ok = false;

// Geometry, computed once.
static GPoint s_center;
static int s_radius;
static GPath *s_hour_path, *s_min_path;
static GPathInfo s_hour_info, s_min_info;
static GPoint s_hour_pts[4], s_min_pts[4];

// ---- helpers ---------------------------------------------------------------

static void build_baton(GPoint *p, int hw, int len, int tail) {
  p[0] = GPoint(-hw, tail);
  p[1] = GPoint(-hw, -len);
  p[2] = GPoint(hw, -len);
  p[3] = GPoint(hw, tail);
}

static GPoint point_at(GPoint center, float deg, int radius) {
  while (deg >= 360.0f) deg -= 360.0f;
  while (deg < 0.0f)    deg += 360.0f;
  int32_t a = (int32_t)((float)TRIG_MAX_ANGLE * deg / 360.0f) & 0xFFFF;
  return GPoint(center.x + (int)((int32_t)radius * sin_lookup(a) / TRIG_MAX_RATIO),
                center.y - (int)((int32_t)radius * cos_lookup(a) / TRIG_MAX_RATIO));
}

static int32_t trig_angle(float deg) {
  return (int32_t)((float)TRIG_MAX_ANGLE * deg / 360.0f);
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

typedef enum { WX_CLEAR, WX_PARTLY, WX_CLOUD, WX_RAIN, WX_SNOW, WX_STORM } WxKind;

static WxKind wx_kind(int code) {
  if (code <= 1) return WX_CLEAR;
  if (code == 2) return WX_PARTLY;
  if (code == 3 || code == 45 || code == 48) return WX_CLOUD;
  if (code >= 95) return WX_STORM;
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return WX_SNOW;
  return WX_RAIN;
}

// A tiny condition glyph centred on `c`.
static void draw_wx_icon(GContext *ctx, GPoint c, WxKind k, GColor fg) {
  graphics_context_set_stroke_color(ctx, fg);
  graphics_context_set_fill_color(ctx, fg);
  graphics_context_set_stroke_width(ctx, 1);
  switch (k) {
    case WX_CLEAR:
      graphics_fill_circle(ctx, c, 5);
      break;
    case WX_PARTLY:
      graphics_draw_circle(ctx, GPoint(c.x - 3, c.y - 2), 4);
      graphics_fill_circle(ctx, GPoint(c.x + 3, c.y + 1), 4);
      break;
    case WX_CLOUD:
      graphics_fill_circle(ctx, GPoint(c.x - 3, c.y), 4);
      graphics_fill_circle(ctx, GPoint(c.x + 3, c.y), 4);
      graphics_fill_rect(ctx, GRect(c.x - 5, c.y, 11, 4), 0, GCornerNone);
      break;
    case WX_RAIN:
      graphics_fill_circle(ctx, GPoint(c.x - 3, c.y - 2), 4);
      graphics_fill_circle(ctx, GPoint(c.x + 3, c.y - 2), 4);
      graphics_draw_line(ctx, GPoint(c.x - 3, c.y + 3), GPoint(c.x - 4, c.y + 6));
      graphics_draw_line(ctx, GPoint(c.x + 2, c.y + 3), GPoint(c.x + 1, c.y + 6));
      break;
    case WX_SNOW:
      graphics_draw_line(ctx, GPoint(c.x - 4, c.y), GPoint(c.x + 4, c.y));
      graphics_draw_line(ctx, GPoint(c.x, c.y - 4), GPoint(c.x, c.y + 4));
      graphics_draw_line(ctx, GPoint(c.x - 3, c.y - 3), GPoint(c.x + 3, c.y + 3));
      graphics_draw_line(ctx, GPoint(c.x - 3, c.y + 3), GPoint(c.x + 3, c.y - 3));
      break;
    case WX_STORM:
      graphics_draw_line(ctx, GPoint(c.x + 2, c.y - 5), GPoint(c.x - 3, c.y + 1));
      graphics_draw_line(ctx, GPoint(c.x - 3, c.y + 1), GPoint(c.x + 1, c.y + 1));
      graphics_draw_line(ctx, GPoint(c.x + 1, c.y + 1), GPoint(c.x - 2, c.y + 6));
      break;
  }
}

static void draw_text_centered(GContext *ctx, const char *s, const char *font_key,
                               GRect box, GColor col) {
  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, s, fonts_get_system_font(font_key), box,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ---- rendering -------------------------------------------------------------

static void face_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GPoint center = s_center;
  int R = s_radius;

  GColor bg  = s_theme_dark ? GColorBlack : GColorWhite;
  GColor fg  = s_theme_dark ? GColorWhite : GColorBlack;
  GColor dim = s_theme_dark ? GColorLightGray : GColorDarkGray;

  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // Ticks: bold bars at the hours, thin strokes at the minutes.
  for (int i = 0; i < 60; i++) {
    bool is_hour = (i % 5 == 0);
    float deg = 360.0f * i / 60.0f;
    graphics_context_set_stroke_color(ctx, fg);
    graphics_context_set_stroke_width(ctx, is_hour ? 4 : 1);
    graphics_draw_line(ctx, point_at(center, deg, R - (is_hour ? 12 : 6)),
                            point_at(center, deg, R));
  }

  int sr = R * 24 / 100;                          // subdial radius
  int f  = R * 46 / 100;                          // subdial centre distance
  GPoint pL = point_at(center, 270, f);           // left  — weather
  GPoint pR = point_at(center, 90, f);            // right — day
  GPoint pB = point_at(center, 180, f);           // bottom — steps/heart

  // AURA wordmark, upper dial.
  if (s_wordmark) {
    GPoint pW = point_at(center, 0, f);
    draw_text_centered(ctx, "AURA", FONT_KEY_GOTHIC_14,
                       GRect(pW.x - 30, pW.y - 10, 60, 18), fg);
  }

  // Subdial rings.
  graphics_context_set_stroke_color(ctx, dim);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, pL, sr);
  graphics_draw_circle(ctx, pR, sr);
  graphics_draw_circle(ctx, pB, sr);

  // LEFT — weather.
  {
    char tbuf[8];
    if (s_wx_ok) {
      draw_wx_icon(ctx, GPoint(pL.x, pL.y - 7), wx_kind(s_wx_code), fg);
      snprintf(tbuf, sizeof(tbuf), "%d\xC2\xB0", s_wx_temp);
    } else {
      snprintf(tbuf, sizeof(tbuf), "--\xC2\xB0");
    }
    draw_text_centered(ctx, tbuf, FONT_KEY_GOTHIC_18_BOLD,
                       GRect(pL.x - sr, pL.y + 2, sr * 2, 20), fg);
  }

  // RIGHT — day, "WWW DD".
  {
    time_t now = time(NULL);
    char d[12];
    strftime(d, sizeof(d), "%a %d", localtime(&now));
    for (int i = 0; i < 3 && d[i]; i++)
      if (d[i] >= 'a' && d[i] <= 'z') d[i] -= 32;   // MON 01
    draw_text_centered(ctx, d, FONT_KEY_GOTHIC_14,
                       GRect(pR.x - sr, pR.y - 9, sr * 2, 20), fg);
  }

  // BOTTOM — steps or heart rate.
  {
    char nbuf[12];
    int v = s_bottom_heart ? current_hr() : today_steps();
    if (v < 0) snprintf(nbuf, sizeof(nbuf), "--");
    else       snprintf(nbuf, sizeof(nbuf), "%d", v);
    draw_text_centered(ctx, nbuf, FONT_KEY_GOTHIC_18,
                       GRect(pB.x - sr, pB.y - 10, sr * 2, 20), fg);
  }

  // Hands (over the subdials, chronograph-style).
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  float hour_deg = 360.0f * (((t->tm_hour % 12) + t->tm_min / 60.0f) / 12.0f);
  float min_deg  = 360.0f * (t->tm_min / 60.0f);

  graphics_context_set_fill_color(ctx, fg);
  gpath_rotate_to(s_hour_path, trig_angle(hour_deg));
  gpath_draw_filled(ctx, s_hour_path);
  gpath_rotate_to(s_min_path, trig_angle(min_deg));
  gpath_draw_filled(ctx, s_min_path);

  if (s_seconds) {
    uint16_t ms = time_ms(NULL, NULL);
    float sec = t->tm_sec + ms / 1000.0f;
    float sdeg = (sec < SWEEP_SECONDS) ? 360.0f * (sec / SWEEP_SECONDS) : 0.0f;
    graphics_context_set_stroke_color(ctx, GColorRed);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, point_at(center, sdeg + 180.0f, R * 22 / 100),
                            point_at(center, sdeg, R * 90 / 100));
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_circle(ctx, point_at(center, sdeg, R * 72 / 100), R * 10 / 100);
  }

  graphics_context_set_fill_color(ctx, fg);
  graphics_fill_circle(ctx, center, 4);
  if (s_seconds) {
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_circle(ctx, center, 2);
  }
}

// ---- update scheduling -----------------------------------------------------

static void schedule_frame(void);
static void frame_cb(void *context) {
  s_timer = NULL;
  layer_mark_dirty(s_face_layer);
  schedule_frame();
}
static void schedule_frame(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  uint16_t ms = time_ms(NULL, NULL);
  float sec = t->tm_sec + ms / 1000.0f;
  s_timer = app_timer_register((sec < SWEEP_SECONDS) ? FRAME_MS : PAUSE_POLL_MS,
                               frame_cb, NULL);
}
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  layer_mark_dirty(s_face_layer);
}

// Start the smooth sweep, or fall back to a per-minute redraw when seconds are off.
static void reconfigure_updates(void) {
  if (s_seconds) {
    tick_timer_service_unsubscribe();
    if (!s_timer) schedule_frame();
  } else {
    if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  }
}

// ---- settings + weather over AppMessage ------------------------------------

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *tp;
  if ((tp = dict_find(iter, MESSAGE_KEY_THEME))) {
    s_theme_dark = tp->value->int32; persist_write_bool(PKEY_THEME, s_theme_dark);
  }
  if ((tp = dict_find(iter, MESSAGE_KEY_SECONDS))) {
    s_seconds = tp->value->int32; persist_write_bool(PKEY_SECONDS, s_seconds);
    reconfigure_updates();
  }
  if ((tp = dict_find(iter, MESSAGE_KEY_WORDMARK))) {
    s_wordmark = tp->value->int32; persist_write_bool(PKEY_WORDMARK, s_wordmark);
  }
  if ((tp = dict_find(iter, MESSAGE_KEY_BOTTOM))) {
    s_bottom_heart = tp->value->int32; persist_write_bool(PKEY_BOTTOM, s_bottom_heart);
  }
  if ((tp = dict_find(iter, MESSAGE_KEY_WX_OK))) {
    s_wx_ok = tp->value->int32; persist_write_bool(PKEY_WX_OK, s_wx_ok);
  }
  if ((tp = dict_find(iter, MESSAGE_KEY_WX_TEMP))) {
    s_wx_temp = tp->value->int32; persist_write_int(PKEY_WX_TEMP, s_wx_temp);
  }
  if ((tp = dict_find(iter, MESSAGE_KEY_WX_CODE))) {
    s_wx_code = tp->value->int32; persist_write_int(PKEY_WX_CODE, s_wx_code);
  }
  layer_mark_dirty(s_face_layer);
}

static void load_settings(void) {
  if (persist_exists(PKEY_SECONDS))  s_seconds = persist_read_bool(PKEY_SECONDS);
  if (persist_exists(PKEY_THEME))    s_theme_dark = persist_read_bool(PKEY_THEME);
  if (persist_exists(PKEY_WORDMARK)) s_wordmark = persist_read_bool(PKEY_WORDMARK);
  if (persist_exists(PKEY_BOTTOM))   s_bottom_heart = persist_read_bool(PKEY_BOTTOM);
  if (persist_exists(PKEY_WX_TEMP))  s_wx_temp = persist_read_int(PKEY_WX_TEMP);
  if (persist_exists(PKEY_WX_CODE))  s_wx_code = persist_read_int(PKEY_WX_CODE);
  if (persist_exists(PKEY_WX_OK))    s_wx_ok = persist_read_bool(PKEY_WX_OK);
}

// ---- window / app ----------------------------------------------------------

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_center = GPoint(bounds.size.w / 2, bounds.size.h / 2);
  s_radius = (bounds.size.w < bounds.size.h ? bounds.size.w : bounds.size.h) / 2 - 6;

  int R = s_radius;
  build_baton(s_hour_pts, 5, R * 50 / 100, R * 12 / 100);
  build_baton(s_min_pts, 4, R * 85 / 100, R * 12 / 100);
  s_hour_info = (GPathInfo){ .num_points = 4, .points = s_hour_pts };
  s_min_info  = (GPathInfo){ .num_points = 4, .points = s_min_pts };
  s_hour_path = gpath_create(&s_hour_info);
  s_min_path  = gpath_create(&s_min_info);
  gpath_move_to(s_hour_path, s_center);
  gpath_move_to(s_min_path, s_center);

  s_face_layer = layer_create(bounds);
  layer_set_update_proc(s_face_layer, face_update_proc);
  layer_add_child(root, s_face_layer);
}

static void window_unload(Window *window) {
  gpath_destroy(s_hour_path);
  gpath_destroy(s_min_path);
  layer_destroy(s_face_layer);
}

static void init(void) {
  load_settings();

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

  reconfigure_updates();
}

static void deinit(void) {
  if (s_timer) app_timer_cancel(s_timer);
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
