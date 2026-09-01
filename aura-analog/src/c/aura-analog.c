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

// Fonts (Liberation Sans, Helvetica-like; Weather Icons for the condition glyph).
static GFont s_font_data;    // bold 18 — temperature / steps / heart
static GFont s_font_label;   // bold 14 — wordmark + day
static GFont s_font_wx;      // Weather Icons 25 — condition glyph

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

// The condition glyph is a Weather Icons codepoint (Private Use Area), rendered
// with the bundled Weather Icons font — proper vector weather symbols rather than
// hand-drawn shapes. WMO codes collapse into a handful of buckets.
static const char *wx_glyph(int code) {
  if (code <= 1)                                  return "\xEF\x80\x8D";  // f00d clear/sunny
  if (code == 2)                                  return "\xEF\x80\x82";  // f002 partly cloudy
  if (code == 45 || code == 48)                   return "\xEF\x80\x94";  // f014 fog
  if (code == 3)                                  return "\xEF\x80\x93";  // f013 cloudy
  if (code >= 95)                                 return "\xEF\x80\x9E";  // f01e thunderstorm
  if ((code >= 71 && code <= 77) ||
      code == 85 || code == 86)                   return "\xEF\x80\x9B";  // f01b snow
  return "\xEF\x80\x99";                                                  // f019 rain
}

// Draw `s` in `font`, optically centred on point `c`. Content size drives the box
// so custom fonts of any size land centred; the small vertical nudge absorbs the
// font's built-in top leading.
static void draw_centered(GContext *ctx, const char *s, GFont font, GPoint c, GColor col) {
  GSize sz = graphics_text_layout_get_content_size(
      s, font, GRect(0, 0, 160, 60), GTextOverflowModeWordWrap, GTextAlignmentCenter);
  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, s, font,
      GRect(c.x - sz.w / 2, c.y - sz.h / 2 - 2, sz.w, sz.h + 4),
      GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

// ---- rendering -------------------------------------------------------------

static void face_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GPoint center = s_center;
  int R = s_radius;

  GColor bg  = s_theme_dark ? GColorBlack : GColorWhite;
  GColor fg  = s_theme_dark ? GColorWhite : GColorBlack;

  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // Minute track: long bold bars at the hours, thin strokes at the minutes —
  // the Mondaine railway dial (no numerals).
  for (int i = 0; i < 60; i++) {
    bool is_hour = (i % 5 == 0);
    float deg = 360.0f * i / 60.0f;
    graphics_context_set_stroke_color(ctx, fg);
    graphics_context_set_stroke_width(ctx, is_hour ? 5 : 1);
    graphics_draw_line(ctx, point_at(center, deg, R - (is_hour ? 17 : 6)),
                            point_at(center, deg, R));
  }

  int f = R * 47 / 100;                            // subdial centre distance
  GPoint pL = point_at(center, 270, f);            // left  — weather
  GPoint pR = point_at(center, 90, f);             // right — day
  GPoint pB = point_at(center, 180, f);            // bottom — steps/heart

  // AURA wordmark, upper dial (no logo — original wordmark).
  if (s_wordmark) {
    draw_centered(ctx, "AURA", s_font_label, point_at(center, 0, R * 42 / 100), fg);
  }

  // Subdials — contents only, no ring outline.
  // LEFT — weather: condition glyph over temperature.
  {
    char tbuf[8];
    if (s_wx_ok) {
      draw_centered(ctx, wx_glyph(s_wx_code), s_font_wx, GPoint(pL.x, pL.y - 8), fg);
      snprintf(tbuf, sizeof(tbuf), "%d\xC2\xB0", s_wx_temp);
    } else {
      snprintf(tbuf, sizeof(tbuf), "--\xC2\xB0");
    }
    draw_centered(ctx, tbuf, s_font_data, GPoint(pL.x, pL.y + 10), fg);
  }

  // RIGHT — day, "WWW DD".
  {
    time_t now = time(NULL);
    char d[12];
    strftime(d, sizeof(d), "%a %d", localtime(&now));
    for (int i = 0; i < 3 && d[i]; i++)
      if (d[i] >= 'a' && d[i] <= 'z') d[i] -= 32;    // MON 01
    draw_centered(ctx, d, s_font_label, pR, fg);
  }

  // BOTTOM — steps or heart rate.
  {
    char nbuf[12];
    int v = s_bottom_heart ? current_hr() : today_steps();
    if (v < 0) snprintf(nbuf, sizeof(nbuf), "--");
    else       snprintf(nbuf, sizeof(nbuf), "%d", v);
    draw_centered(ctx, nbuf, s_font_data, pB, fg);
  }

  // Hands (over the subdials, chronograph-style). Take seconds and milliseconds
  // from a single time_ms() sample — reading tm_sec and ms from two separate
  // clock reads makes them disagree at the second boundary, which snaps the
  // sweeping hand backwards ~5° once per second.
  time_t now;
  uint16_t ms = time_ms(&now, NULL);
  struct tm *t = localtime(&now);
  float hour_deg = 360.0f * (((t->tm_hour % 12) + t->tm_min / 60.0f) / 12.0f);
  float min_deg  = 360.0f * (t->tm_min / 60.0f);

  graphics_context_set_fill_color(ctx, fg);
  gpath_rotate_to(s_hour_path, trig_angle(hour_deg));
  gpath_draw_filled(ctx, s_hour_path);
  gpath_rotate_to(s_min_path, trig_angle(min_deg));
  gpath_draw_filled(ctx, s_min_path);

  if (s_seconds) {
    // The seconds and milliseconds fields are latched from separate reads, so
    // near a boundary they disagree (e.g. tm_sec=2 while ms has already wrapped
    // to 6 — real time 3.006). Reading tm_sec+ms directly snaps the hand back
    // ~1 s each second. Instead keep our own second-of-minute: advance it when
    // ms wraps downward, and resync to the OS clock only mid-second, where the
    // two fields provably agree.
    static int s_disp_sec = -1;
    static uint16_t s_prev_ms = 0;
    if (s_disp_sec < 0) s_disp_sec = t->tm_sec;                    // seed once
    if (ms + 200 < s_prev_ms) s_disp_sec = (s_disp_sec + 1) % 60;  // ms wrapped: +1s
    s_prev_ms = ms;
    // The OS seconds field lags the ms field by a variable fraction of a second,
    // so only correct genuine drift (>2 s), never that ±1 s boundary lag.
    int drift = (s_disp_sec - t->tm_sec + 60) % 60;
    if (drift > 2 && drift < 58) s_disp_sec = t->tm_sec;

    float sec = s_disp_sec + ms / 1000.0f;
    float sdeg = (sec < SWEEP_SECONDS) ? 360.0f * (sec / SWEEP_SECONDS) : 0.0f;
    // Mondaine second hand: a thin red stem with a counterweight tail, ending in
    // the red signal disc (a lollipop) — the stem stops at the disc, no pin past it.
    GPoint disc = point_at(center, sdeg, R * 73 / 100);
    graphics_context_set_stroke_color(ctx, GColorRed);
    graphics_context_set_stroke_width(ctx, 2);
    graphics_draw_line(ctx, point_at(center, sdeg + 180.0f, R * 18 / 100), disc);
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_circle(ctx, disc, R * 11 / 100);
  }

  // Hub.
  graphics_context_set_fill_color(ctx, fg);
  graphics_fill_circle(ctx, center, 5);
  if (s_seconds) {
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_circle(ctx, center, 3);
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
  build_baton(s_hour_pts, 6, R * 55 / 100, R * 16 / 100);
  build_baton(s_min_pts, 5, R * 90 / 100, R * 16 / 100);
  s_hour_info = (GPathInfo){ .num_points = 4, .points = s_hour_pts };
  s_min_info  = (GPathInfo){ .num_points = 4, .points = s_min_pts };
  s_hour_path = gpath_create(&s_hour_info);
  s_min_path  = gpath_create(&s_min_info);
  gpath_move_to(s_hour_path, s_center);
  gpath_move_to(s_min_path, s_center);

  s_font_data  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_AURA_18));
  s_font_label = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_AURA_14));
  s_font_wx    = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_WI_25));

  s_face_layer = layer_create(bounds);
  layer_set_update_proc(s_face_layer, face_update_proc);
  layer_add_child(root, s_face_layer);
}

static void window_unload(Window *window) {
  fonts_unload_custom_font(s_font_data);
  fonts_unload_custom_font(s_font_label);
  fonts_unload_custom_font(s_font_wx);
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
