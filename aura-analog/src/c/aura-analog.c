#include <pebble.h>

// Aura — analog watchface (Phase 2): the stop-to-go face.
//
// Reproduces the Swiss-Railways "stop2go" seconds mechanic on an ORIGINAL Aura
// dial (dark, not Mondaine's white; a thin accent second hand, not the
// trademarked red lollipop). Per-minute cycle, with `sec` the seconds+ms
// elapsed in the current minute:
//   * SWEEP  sec in [0, 58): the hand travels a full 360 deg over 58 s, so it
//            runs slightly fast.
//   * PAUSE  sec in [58, 60): it holds at 12 o'clock for ~2 s.
//   * At :00 the minute hand jumps and the second hand releases from 12 —
//            continuous, no visible snap.
//
// Standalone: no phone data. The accent colour tracks the time of day, the same
// Aura "sky" ramp the digital face uses (see docs/PALETTE.md).

#define SWEEP_SECONDS 58.0f     // remaining ~2 s is the pause at 12
#define FRAME_MS 50             // ~20 fps while sweeping
#define PAUSE_POLL_MS 200       // relaxed cadence while the hand is parked
#define KEY_SHOW_SECONDS 1      // persist key a future config screen can write

static Window *s_window;
static Layer *s_face_layer;
static AppTimer *s_timer;
static bool s_show_seconds = true;

// Time-of-day accent ramp — the Aura sky, quantised to the Pebble palette.
// (Copied per-project by design; the faces intentionally share no library.)
static GColor accent_for_hour(int hour) {
  if (hour < 5)  return GColorIndigo;
  if (hour < 7)  return GColorSunsetOrange;
  if (hour < 10) return GColorPictonBlue;
  if (hour < 15) return GColorVividCerulean;
  if (hour < 18) return GColorRajah;
  if (hour < 21) return GColorSunsetOrange;
  return GColorIndigo;
}

// Point on a circle of the given radius at `deg` clockwise from 12 o'clock.
static GPoint point_at(GPoint center, float deg, int radius) {
  while (deg >= 360.0f) deg -= 360.0f;
  while (deg < 0.0f)    deg += 360.0f;
  int32_t a = (int32_t)((float)TRIG_MAX_ANGLE * deg / 360.0f) & 0xFFFF;
  int32_t s = sin_lookup(a);
  int32_t c = cos_lookup(a);
  return GPoint(center.x + (int)((int32_t)radius * s / TRIG_MAX_RATIO),
                center.y - (int)((int32_t)radius * c / TRIG_MAX_RATIO));
}

static void draw_hand(GContext *ctx, GPoint center, float deg,
                      int len, int tail, int width, GColor color) {
  graphics_context_set_stroke_color(ctx, color);
  graphics_context_set_stroke_width(ctx, width);
  GPoint tip = point_at(center, deg, len);
  GPoint back = tail > 0 ? point_at(center, deg + 180.0f, tail) : center;
  graphics_draw_line(ctx, back, tip);
}

static void face_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  GPoint center = GPoint(b.size.w / 2, b.size.h / 2);
  int R = (b.size.w < b.size.h ? b.size.w : b.size.h) / 2 - 6;

  // Dark Aura dial.
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  // Ticks: bold white at the hours, thin grey at the minutes.
  for (int i = 0; i < 60; i++) {
    bool is_hour = (i % 5 == 0);
    float deg = 360.0f * i / 60.0f;
    graphics_context_set_stroke_color(ctx, is_hour ? GColorWhite : GColorDarkGray);
    graphics_context_set_stroke_width(ctx, is_hour ? 3 : 1);
    graphics_draw_line(ctx, point_at(center, deg, R - (is_hour ? 10 : 5)),
                            point_at(center, deg, R));
  }

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  // Hour hand sweeps smoothly with the minutes; minute hand JUMPS each minute
  // (the Mondaine behaviour), so it's driven by tm_min alone.
  float hour_deg = 360.0f * (((t->tm_hour % 12) + t->tm_min / 60.0f) / 12.0f);
  float min_deg  = 360.0f * (t->tm_min / 60.0f);

  draw_hand(ctx, center, hour_deg, R * 55 / 100, R * 15 / 100, 6, GColorWhite);
  draw_hand(ctx, center, min_deg,  R * 82 / 100, R * 15 / 100, 4, GColorWhite);

  // Second hand — the stop-to-go mechanic — in the time-of-day accent.
  if (s_show_seconds) {
    uint16_t ms = time_ms(NULL, NULL);
    float sec = t->tm_sec + ms / 1000.0f;
    float sdeg = (sec < SWEEP_SECONDS) ? 360.0f * (sec / SWEEP_SECONDS) : 0.0f;

    GColor accent = accent_for_hour(t->tm_hour);
    draw_hand(ctx, center, sdeg, R * 88 / 100, R * 20 / 100, 2, accent);
    graphics_context_set_fill_color(ctx, accent);
    graphics_fill_circle(ctx, center, 4);
  }

  // Center pin.
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, center, 2);
}

// Frame pump: fast while the hand sweeps, relaxed while it's parked at 12, so
// the 2 s pause each minute isn't redrawn 20x/s for nothing.
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
  uint32_t delay = (sec < SWEEP_SECONDS) ? FRAME_MS : PAUSE_POLL_MS;
  s_timer = app_timer_register(delay, frame_cb, NULL);
}

// Used only when the second hand is disabled: a plain per-minute redraw.
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  layer_mark_dirty(s_face_layer);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_face_layer = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_face_layer, face_update_proc);
  layer_add_child(root, s_face_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_face_layer);
}

static void init(void) {
  s_show_seconds = persist_exists(KEY_SHOW_SECONDS)
                       ? persist_read_bool(KEY_SHOW_SECONDS) : true;

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  if (s_show_seconds) {
    schedule_frame();                                  // smooth sweep
  } else {
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);  // hands only
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
