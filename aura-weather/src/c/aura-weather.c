#include <pebble.h>
#include "weather.h"
#include "sky.h"

// Aura Weather - a standalone weather watchapp for the Pebble Time 2.
// Step 1: the scaffold, the signature hero screen and the sun-path maths, fed by
// hard-coded data so the look can be proven on the emulator before any phone
// bridge exists (docs/00 build order, step 1). UP/DOWN page the three screens.

#define SCREEN_HERO   0
#define SCREEN_HOURLY 1
#define SCREEN_DAILY  2
#define SCREEN_COUNT  3

static Window    *s_window;
static Layer     *s_canvas;
static int        s_screen = SCREEN_HERO;

static GFont s_font_big;    // large current temperature
static GFont s_font_text;   // labels, hi/lo, location, weekday
static GFont s_font_small;  // staleness note
static GFont s_font_wx;     // condition glyph, hero size
static GFont s_font_wx_sm;  // condition glyph, list size

static Weather s_wx;        // the single in-memory forecast (hard-coded in step 1)

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

// ---- hero screen -----------------------------------------------------------

static void draw_hero(GContext *ctx, GRect b) {
  GRect sky = GRect(b.origin.x, b.origin.y, b.size.w, b.size.h * 62 / 100);
  time_t now = time(NULL);
  SunState sun = sun_compute(now, s_wx.sunrise, s_wx.sunset, sky);
  sky_draw(ctx, sky, sun);

  // Dark readout band under the sky so text stays legible over any sky colour.
  int ry = sky.origin.y + sky.size.h;
  GRect readout = GRect(b.origin.x, ry, b.size.w, b.origin.y + b.size.h - ry);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, readout, 0, GCornerNone);

  bool night = is_night_at(now);
  char buf[32];

  // Condition glyph, top-left of the readout band.
  draw_text_in(ctx, wx_glyph(s_wx.code, night), s_font_wx,
               GRect(readout.origin.x + 4, ry - 2, 40, 40), GColorWhite, GTextAlignmentLeft);

  // Current temperature, large and centred.
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", s_wx.temp);
  draw_text_in(ctx, buf, s_font_big, GRect(readout.origin.x, ry - 6, readout.size.w, 40),
               temp_color(s_wx.temp, s_wx.is_metric), GTextAlignmentCenter);

  // Hi / lo, tinted by the ramp, just under the temperature.
  snprintf(buf, sizeof(buf), "%d\xC2\xB0 / %d\xC2\xB0", s_wx.tmax, s_wx.tmin);
  draw_text_in(ctx, buf, s_font_text, GRect(readout.origin.x, ry + 34, readout.size.w, 22),
               GColorWhite, GTextAlignmentCenter);

  // Location name near the bottom.
  draw_text_in(ctx, s_wx.name, s_font_text,
               GRect(readout.origin.x + 4, readout.origin.y + readout.size.h - 24,
                     readout.size.w - 8, 22),
               GColorWhite, GTextAlignmentCenter);

  // Staleness note if the forecast is old (> 90 min) or absent.
  if (s_wx.updated == 0) {
    draw_text_in(ctx, "no data", s_font_small,
                 GRect(readout.origin.x + 4, ry + 2, readout.size.w - 8, 16),
                 GColorLightGray, GTextAlignmentRight);
  }
}

// ---- placeholder forecast screens (real rows land in step 3) ---------------

static void draw_placeholder(GContext *ctx, GRect b, const char *title) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);
  draw_text_in(ctx, title, s_font_text,
               GRect(b.origin.x, b.origin.y + b.size.h / 2 - 14, b.size.w, 28),
               GColorWhite, GTextAlignmentCenter);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  switch (s_screen) {
    case SCREEN_HERO:   draw_hero(ctx, b); break;
    case SCREEN_HOURLY: draw_placeholder(ctx, b, "Hourly"); break;
    case SCREEN_DAILY:  draw_placeholder(ctx, b, "Daily"); break;
  }
}

// ---- input + ticks ---------------------------------------------------------

static void up_click(ClickRecognizerRef r, void *ctx) {
  s_screen = (s_screen + SCREEN_COUNT - 1) % SCREEN_COUNT;
  layer_mark_dirty(s_canvas);
}
static void down_click(ClickRecognizerRef r, void *ctx) {
  s_screen = (s_screen + 1) % SCREEN_COUNT;
  layer_mark_dirty(s_canvas);
}
static void select_click(ClickRecognizerRef r, void *ctx) {
  // Step 2 will send a "refresh now" AppMessage here; for now just redraw.
  layer_mark_dirty(s_canvas);
}
static void click_config(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
}

static void tick_handler(struct tm *t, TimeUnits units) {
  if (s_screen == SCREEN_HERO) layer_mark_dirty(s_canvas);   // the sun/moon moves
}

// ---- demo data (step 1 only) -----------------------------------------------

static void seed_demo_weather(void) {
  memset(&s_wx, 0, sizeof(s_wx));
  strncpy(s_wx.name, "Madrid", sizeof(s_wx.name) - 1);
  s_wx.temp = 22; s_wx.tmin = 14; s_wx.tmax = 27;
  s_wx.code = WX_FEW; s_wx.humidity = 45; s_wx.pop = 10;
  s_wx.is_metric = 1; s_wx.updated = time(NULL);

  // Sunrise 07:15, sunset 21:05 local today, so the arc is meaningful on launch.
  time_t now = time(NULL);
  struct tm lt = *localtime(&now);
  lt.tm_hour = 7; lt.tm_min = 15; lt.tm_sec = 0; s_wx.sunrise = mktime(&lt);
  lt.tm_hour = 21; lt.tm_min = 5;                s_wx.sunset  = mktime(&lt);

  for (int i = 0; i < HOURS_N; i++) {
    s_wx.hours[i] = (HourSlot){ .temp = 20 + i, .code = (i % 3 == 0) ? WX_RAIN : WX_FEW,
                                .pop = (i % 3 == 0) ? 40 : 10 };
  }
  for (int i = 0; i < DAYS_N; i++) {
    s_wx.days[i] = (DaySlot){ .min = 12 + i, .max = 24 + i,
                              .code = (i % 2) ? WX_CLOUDY : WX_CLEAR, .pop = i * 8 };
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
  s_font_big    = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_AURA_34));
  s_font_text   = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_AURA_18));
  s_font_small  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_AURA_14));
  s_font_wx     = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_WI_30));
  s_font_wx_sm  = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_WI_20));

  seed_demo_weather();

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load, .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
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
