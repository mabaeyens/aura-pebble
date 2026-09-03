#include <pebble.h>
#include "weather.h"
#include "sky.h"
#include "message_keys.auto.h"

// Aura Weather - a standalone weather watchapp for the Pebble Time 2.
// Step 2: the live Open-Meteo bridge. The phone (src/pkjs) fetches, normalises
// and ships the forecast in framed AppMessages; this side fills the struct,
// persists it, and renders. SELECT requests a refresh. UP/DOWN page the three
// screens; the hero (step 1) draws the procedural sun over the sky.

#define SCREEN_HERO   0
#define SCREEN_HOURLY 1
#define SCREEN_DAILY  2
#define SCREEN_COUNT  3

#define PKEY_WEATHER 1   // persist slot for the whole Weather struct

static Window    *s_window;
static Layer     *s_canvas;
static int        s_screen = SCREEN_HERO;

static GFont s_font_big;    // large current temperature
static GFont s_font_text;   // labels, hi/lo, location, weekday
static GFont s_font_small;  // staleness note
static GFont s_font_wx;     // condition glyph, hero size
static GFont s_font_wx_sm;  // condition glyph, list size

static Weather s_wx;        // the single in-memory forecast (from the phone / persist)

static void request_refresh(void);

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
  request_refresh();
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

// ---- persistence + first-run default ---------------------------------------

static void persist_save(void) {
  persist_write_data(PKEY_WEATHER, &s_wx, sizeof(s_wx));
}

// A sensible first-run state so the signature shows before any phone data:
// today's default sun times draw the arc, updated == 0 reads as "no data".
static void seed_default(void) {
  memset(&s_wx, 0, sizeof(s_wx));
  strncpy(s_wx.name, "Aura", sizeof(s_wx.name) - 1);
  s_wx.code = WX_FEW; s_wx.is_metric = 1; s_wx.updated = 0;
  time_t now = time(NULL);
  struct tm lt = *localtime(&now);
  lt.tm_hour = 7;  lt.tm_min = 0; lt.tm_sec = 0; s_wx.sunrise = mktime(&lt);
  lt.tm_hour = 21; lt.tm_min = 0;                s_wx.sunset  = mktime(&lt);
}

static void load_weather(void) {
  if (persist_exists(PKEY_WEATHER) &&
      persist_get_size(PKEY_WEATHER) == (int)sizeof(s_wx)) {
    persist_read_data(PKEY_WEATHER, &s_wx, sizeof(s_wx));
  } else {
    seed_default();
  }
}

// ---- AppMessage inbox ------------------------------------------------------

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;

  // Current / handshake message.
  if ((t = dict_find(iter, MESSAGE_KEY_WX_OK))) {
    if (t->value->int32 == 0) return;   // provider error: keep the persisted struct
    Tuple *tp;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_NAME)))
      strncpy(s_wx.name, tp->value->cstring, sizeof(s_wx.name) - 1);
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_TEMP)))    s_wx.temp     = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_TMIN)))    s_wx.tmin     = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_TMAX)))    s_wx.tmax     = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_CODE)))    s_wx.code     = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_HUM)))     s_wx.humidity = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_POP)))     s_wx.pop      = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_SUNRISE))) s_wx.sunrise  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_SUNSET)))  s_wx.sunset   = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_UNITS)))   s_wx.is_metric = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_UPDATED))) s_wx.updated  = tp->value->int32;
    persist_save();
    layer_mark_dirty(s_canvas);
    return;
  }

  // Hourly frame: one slot per message, keyed by index.
  if ((t = dict_find(iter, MESSAGE_KEY_H_IDX))) {
    int i = t->value->int32;
    if (i >= 0 && i < HOURS_N) {
      Tuple *tp;
      if ((tp = dict_find(iter, MESSAGE_KEY_H_TEMP))) s_wx.hours[i].temp = tp->value->int32;
      if ((tp = dict_find(iter, MESSAGE_KEY_H_CODE))) s_wx.hours[i].code = tp->value->int32;
      if ((tp = dict_find(iter, MESSAGE_KEY_H_POP)))  s_wx.hours[i].pop  = tp->value->int32;
    }
    layer_mark_dirty(s_canvas);
    return;
  }

  // Daily frame: one slot per message. Persist once the last day lands.
  if ((t = dict_find(iter, MESSAGE_KEY_D_IDX))) {
    int i = t->value->int32;
    if (i >= 0 && i < DAYS_N) {
      Tuple *tp;
      if ((tp = dict_find(iter, MESSAGE_KEY_D_MIN)))  s_wx.days[i].min  = tp->value->int32;
      if ((tp = dict_find(iter, MESSAGE_KEY_D_MAX)))  s_wx.days[i].max  = tp->value->int32;
      if ((tp = dict_find(iter, MESSAGE_KEY_D_CODE))) s_wx.days[i].code = tp->value->int32;
      if ((tp = dict_find(iter, MESSAGE_KEY_D_POP)))  s_wx.days[i].pop  = tp->value->int32;
    }
    if (i == DAYS_N - 1) persist_save();
    layer_mark_dirty(s_canvas);
    return;
  }
}

// Ask the phone for a fresh forecast (SELECT, and on launch the phone self-starts).
static void request_refresh(void) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) == APP_MSG_OK) {
    dict_write_uint8(out, MESSAGE_KEY_REFRESH, 1);
    app_message_outbox_send();
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

  load_weather();

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, click_config);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load, .unload = window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);

  app_message_register_inbox_received(inbox_received);
  app_message_open(512, 64);   // inbox holds the current frame (name + ints) comfortably
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
