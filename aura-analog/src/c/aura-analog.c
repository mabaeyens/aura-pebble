#include <pebble.h>

// Aura — analog watchface (Phase 2): the stop-to-go railway face.
//
// A faithful Swiss-Railways-style clock: white dial, black baton hour/minute
// hands, and a thin red second hand carrying the red "lollipop" disc. See the
// design-origin note in README.md — this is a close homage to the Mondaine
// railway clock, whose look is a registered design.
//
// The "stop2go" seconds mechanic, per-minute cycle (`sec` = seconds+ms elapsed
// in the current minute):
//   * SWEEP  sec in [0, 58): the hand travels a full 360 deg over 58 s (so it
//            runs slightly fast).
//   * PAUSE  sec in [58, 60): it holds at 12 o'clock for ~2 s.
//   * At :00 the minute hand jumps and the second hand releases from 12 —
//            continuous, no visible snap.
//
// Standalone: no phone data.

#define SWEEP_SECONDS 58.0f
#define FRAME_MS 50             // ~20 fps while sweeping
#define PAUSE_POLL_MS 200       // relaxed cadence while parked at 12
#define KEY_SHOW_SECONDS 1      // persist key a future config screen can write

static Window *s_window;
static Layer *s_face_layer;
static AppTimer *s_timer;
static bool s_show_seconds = true;
static GPoint s_center;
static int s_radius;

static GPath *s_hour_path;
static GPath *s_min_path;
static GPathInfo s_hour_info;
static GPathInfo s_min_info;
static GPoint s_hour_pts[4];
static GPoint s_min_pts[4];

// A blunt baton pointing up (12 o'clock), centred on the origin: `hw` half-width,
// `len` from centre to tip, `tail` extending behind the centre.
static void build_baton(GPoint *p, int hw, int len, int tail) {
  p[0] = GPoint(-hw, tail);
  p[1] = GPoint(-hw, -len);
  p[2] = GPoint(hw, -len);
  p[3] = GPoint(hw, tail);
}

// Point on a circle of the given radius at `deg` clockwise from 12 o'clock.
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

static void face_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GPoint center = s_center;
  int R = s_radius;

  // White railway dial.
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // Black ticks: bold bars at the hours, thin strokes at the minutes.
  for (int i = 0; i < 60; i++) {
    bool is_hour = (i % 5 == 0);
    float deg = 360.0f * i / 60.0f;
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, is_hour ? 4 : 1);
    graphics_draw_line(ctx, point_at(center, deg, R - (is_hour ? 12 : 6)),
                            point_at(center, deg, R));
  }

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  // Hour hand sweeps with the minutes; minute hand JUMPS each minute.
  float hour_deg = 360.0f * (((t->tm_hour % 12) + t->tm_min / 60.0f) / 12.0f);
  float min_deg  = 360.0f * (t->tm_min / 60.0f);

  graphics_context_set_fill_color(ctx, GColorBlack);
  gpath_rotate_to(s_hour_path, trig_angle(hour_deg));
  gpath_draw_filled(ctx, s_hour_path);
  gpath_rotate_to(s_min_path, trig_angle(min_deg));
  gpath_draw_filled(ctx, s_min_path);

  // Red second hand with the lollipop disc — the stop-to-go mechanic.
  if (s_show_seconds) {
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

  // Centre hub.
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, center, 4);
  if (s_show_seconds) {
    graphics_context_set_fill_color(ctx, GColorRed);
    graphics_fill_circle(ctx, center, 2);
  }
}

// Frame pump: fast while sweeping, relaxed while parked at 12.
static void schedule_frame(void);
static void frame_cb(void *context) {
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

// Used only when the second hand is disabled: a plain per-minute redraw.
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  layer_mark_dirty(s_face_layer);
}

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
  s_show_seconds = persist_exists(KEY_SHOW_SECONDS)
                       ? persist_read_bool(KEY_SHOW_SECONDS) : true;

  s_window = window_create();
  window_set_background_color(s_window, GColorWhite);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  if (s_show_seconds) {
    schedule_frame();
  } else {
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  }
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
