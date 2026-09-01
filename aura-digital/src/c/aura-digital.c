#include <pebble.h>

// Aura digital watchface (Phase 1)
// Minimalist glanceable face: step count, time, date, battery. System fonts
// only, so it carries zero bundled resources and stays trivially within budget.
//
// Aura identity: the accent colour tracks the TIME OF DAY, echoing Aura's
// live sun-tracking sky on the phone (deep indigo at night -> warm orange at
// dawn/dusk -> bright cerulean at midday). Steps and the accent rule share
// that colour, so the whole face drifts through the day together. All values
// snapped to the 64-colour Pebble palette (see docs/PALETTE.md).
//
// Fully standalone: no phone bridge, no weather. Live weather arrives in Phase 3.

static Window *s_window;
static TextLayer *s_steps_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_batt_layer;
static Layer *s_accent_layer;
static Layer *s_bt_layer;

static GColor s_accent;                 // recomputed each minute from the hour
static BatteryChargeState s_battery;
static bool s_connected = true;         // phone connection (Bluetooth)
static char s_steps_buf[24];
static char s_time_buf[8];
static char s_date_buf[24];
static char s_batt_buf[8];

// Time-of-day accent ramp: the Aura sky, quantised to the Pebble palette.
static GColor accent_for_hour(int hour) {
  if (hour < 5)  return GColorIndigo;        // deep night   #5500AA
  if (hour < 7)  return GColorSunsetOrange;  // dawn         #FF5555
  if (hour < 10) return GColorPictonBlue;    // morning      #55AAFF
  if (hour < 15) return GColorVividCerulean; // midday       #00AAFF
  if (hour < 18) return GColorRajah;         // afternoon    #FFAA55
  if (hour < 21) return GColorSunsetOrange;  // dusk         #FF5555
  return GColorIndigo;                       // night        #5500AA
}

static int today_steps(void) {
#if defined(PBL_HEALTH)
  time_t start = time_start_of_today();
  time_t end = time(NULL);
  HealthServiceAccessibilityMask mask =
      health_service_metric_accessible(HealthMetricStepCount, start, end);
  if (mask & HealthServiceAccessibilityMaskAvailable) {
    return (int)health_service_sum_today(HealthMetricStepCount);
  }
#endif
  return -1; // unavailable on this platform / not yet granted
}

static void accent_update_proc(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, s_accent);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
}

// A small red dot, top-right, only while the phone is out of range.
static void bt_update_proc(Layer *layer, GContext *ctx) {
  if (s_connected) return;
  GRect b = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_fill_circle(ctx, GPoint(b.size.w / 2, b.size.h / 2), 4);
}

static void connection_handler(bool connected) {
  s_connected = connected;
  layer_mark_dirty(s_bt_layer);
}

static void update_display(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  // Accent colour for the current hour.
  s_accent = accent_for_hour(t->tm_hour);
  text_layer_set_text_color(s_steps_layer, s_accent);
  layer_mark_dirty(s_accent_layer);

  // Time. Format from the integer hour so 12h mode has no leading zero
  // ("6:58", not "06:58") without any string surgery.
  if (clock_is_24h_style()) {
    snprintf(s_time_buf, sizeof(s_time_buf), "%02d:%02d", t->tm_hour, t->tm_min);
  } else {
    int h12 = t->tm_hour % 12;
    if (h12 == 0) h12 = 12;
    snprintf(s_time_buf, sizeof(s_time_buf), "%d:%02d", h12, t->tm_min);
  }
  text_layer_set_text(s_time_layer, s_time_buf);

  // Date.
  strftime(s_date_buf, sizeof(s_date_buf), "%a %d %b", t);
  text_layer_set_text(s_date_layer, s_date_buf);

  // Steps.
  int steps = today_steps();
  if (steps < 0) {
    layer_set_hidden(text_layer_get_layer(s_steps_layer), true);
  } else {
    layer_set_hidden(text_layer_get_layer(s_steps_layer), false);
    // Thousands separator without locale machinery, formatted in one pass.
    if (steps >= 1000) {
      snprintf(s_steps_buf, sizeof(s_steps_buf), "%d,%03d steps",
               steps / 1000, steps % 1000);
    } else {
      snprintf(s_steps_buf, sizeof(s_steps_buf), "%d steps", steps);
    }
    text_layer_set_text(s_steps_layer, s_steps_buf);
  }
}

static void update_battery(void) {
  if (s_battery.is_charging) {
    snprintf(s_batt_buf, sizeof(s_batt_buf), "+%d%%", s_battery.charge_percent);
    text_layer_set_text_color(s_batt_layer, GColorMediumSpringGreen);
  } else {
    snprintf(s_batt_buf, sizeof(s_batt_buf), "%d%%", s_battery.charge_percent);
    // Warn in red at 20% or below; otherwise a quiet grey.
    text_layer_set_text_color(s_batt_layer,
        s_battery.charge_percent <= 20 ? GColorRed : GColorLightGray);
  }
  text_layer_set_text(s_batt_layer, s_batt_buf);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_display();
}

static void battery_handler(BatteryChargeState state) {
  s_battery = state;
  update_battery();
}

static TextLayer *make_text_layer(Layer *root, GRect frame, const char *font_key,
                                  GColor color, GTextAlignment align) {
  TextLayer *l = text_layer_create(frame);
  text_layer_set_background_color(l, GColorClear);
  text_layer_set_text_color(l, color);
  text_layer_set_font(l, fonts_get_system_font(font_key));
  text_layer_set_text_alignment(l, align);
  layer_add_child(root, text_layer_get_layer(l));
  return l;
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);
  window_set_background_color(window, GColorBlack);

  int cy = b.size.h / 2;
  int rule_w = (b.size.w * 44) / 100;

  // Steps: top, in the accent colour.
  s_steps_layer = make_text_layer(root, GRect(0, 20, b.size.w, 24),
      FONT_KEY_GOTHIC_18_BOLD, GColorWhite, GTextAlignmentCenter);

  // Bluetooth-disconnect indicator: top-right corner.
  s_bt_layer = layer_create(GRect(b.size.w - 20, 8, 12, 12));
  layer_set_update_proc(s_bt_layer, bt_update_proc);
  layer_add_child(root, s_bt_layer);

  // Time: centred, large.
  s_time_layer = make_text_layer(root, GRect(0, cy - 34, b.size.w, 42),
      FONT_KEY_LECO_38_BOLD_NUMBERS, GColorWhite, GTextAlignmentCenter);

  // Accent rule: the time-of-day colour.
  s_accent_layer = layer_create(GRect((b.size.w - rule_w) / 2, cy + 14, rule_w, 4));
  layer_set_update_proc(s_accent_layer, accent_update_proc);
  layer_add_child(root, s_accent_layer);

  // Date: below the rule.
  s_date_layer = make_text_layer(root, GRect(0, cy + 22, b.size.w, 26),
      FONT_KEY_GOTHIC_18, GColorLightGray, GTextAlignmentCenter);

  // Battery: bottom.
  s_batt_layer = make_text_layer(root, GRect(0, b.size.h - 30, b.size.w, 22),
      FONT_KEY_GOTHIC_14, GColorLightGray, GTextAlignmentCenter);

  update_display();
  update_battery();
}

static void window_unload(Window *window) {
  text_layer_destroy(s_steps_layer);
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_batt_layer);
  layer_destroy(s_accent_layer);
  layer_destroy(s_bt_layer);
}

static void init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  s_battery = battery_state_service_peek();

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  s_connected = connection_service_peek_pebble_app_connection();
  connection_service_subscribe((ConnectionHandlers){
    .pebble_app_connection_handler = connection_handler,
  });
  // Steps refresh on the minute tick: the right cadence for a watchface; no
  // need for a separate high-frequency health-event subscription.
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
