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

static GFont s_font_xl;     // extra-large hero temperature
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

// Draw text with a 1px black halo so it stays legible floating over the scene
// (Pebble has no soft shadow/blur; four cardinal offsets read as a clean edge).
static void draw_text_halo(GContext *ctx, const char *s, GFont f, GRect box,
                           GColor col, GTextAlignment align) {
  graphics_context_set_text_color(ctx, GColorBlack);
  static const int dx[] = { -1, 1, 0, 0 }, dy[] = { 0, 0, -1, 1 };
  for (int i = 0; i < 4; i++) {
    graphics_draw_text(ctx, s, f, GRect(box.origin.x + dx[i], box.origin.y + dy[i],
                                        box.size.w, box.size.h),
                       GTextOverflowModeTrailingEllipsis, align, NULL);
  }
  graphics_context_set_text_color(ctx, col);
  graphics_draw_text(ctx, s, f, box, GTextOverflowModeTrailingEllipsis, align, NULL);
}

// One-word-ish condition summary shown under the temperature (the hero headline).
static const char *wx_summary(uint8_t code) {
  switch (code) {
    case WX_CLEAR:    return "Clear";
    case WX_FEW:      return "Few clouds";
    case WX_CLOUDY:   return "Cloudy";
    case WX_OVERCAST: return "Overcast";
    case WX_FOG:      return "Fog";
    case WX_DRIZZLE:  return "Drizzle";
    case WX_RAIN:     return "Rain";
    case WX_HEAVY:    return "Heavy rain";
    case WX_SNOW:     return "Snow";
    case WX_THUNDER:  return "Thunderstorm";
    default:          return "";
  }
}

// A severe-weather warning label, or NULL when nothing is worth flagging. The
// phone bridge carries no alert field yet, so this is derived from the condition
// itself; a real AEMET aviso feed would replace it later.
static const char *wx_warning(uint8_t code) {
  switch (code) {
    case WX_THUNDER: return "STORM";
    case WX_HEAVY:   return "HEAVY RAIN";
    case WX_SNOW:    return "SNOW";
    case WX_FOG:     return "FOG";
    default:         return NULL;
  }
}

static GColor warning_color(uint8_t code) {
  switch (code) {
    case WX_SNOW: return GColorCeleste;       // pale blue
    case WX_FOG:  return GColorLightGray;
    default:      return GColorChromeYellow;  // storm / heavy rain: hazard amber
  }
}

// ---- hero screen -----------------------------------------------------------

static void draw_hero(GContext *ctx, GRect b) {
  time_t now = time(NULL);
  // The sun arcs over the full frame; the scenery then occludes it near the
  // horizon, exactly as the phone hero composes sky -> disc -> landscape.
  SunState sun = sun_compute(now, s_wx.sunrise, s_wx.sunset, b);
  sky_draw(ctx, b, sun, s_wx.code);
  scene_draw(ctx, b, sun, s_wx.code);

  int Y = b.origin.y, W = b.size.w, H = b.size.h;
  int pad = 6, X = b.origin.x + pad, tw = W - pad * 2;
  char buf[32];

  // The Aura hero overlay floats over the scene (no card frame), left-aligned
  // like the phone app: location top-left, then the big temperature, condition
  // summary, hi/lo, and a warning pill for severe weather.

  // A tight left-aligned cluster near the top (like the phone hero). Each line's
  // box top is placed by hand so the *visible* gap between glyphs is equal: the
  // XL temp font carries heavy internal top padding, so its box is pulled up hard
  // (its digits render well below the box top).
  // Box tops solved from each font's measured glyph offsets so the *visible* gap
  // between every pair of lines is a uniform 7px (the big temp box sits high
  // because its digits render ~14px below the box top).
  int y_loc  = Y +  6;   // location -> glyphs 10..28
  int y_temp = Y + 21;   // big temp -> digits 35..69
  int y_sum  = Y + 72;   // summary  -> glyphs 76..90
  int y_hilo = Y + 93;   // hi/lo    -> glyphs 97..111

  // Location, top-left, small.
  draw_text_halo(ctx, s_wx.name, s_font_text, GRect(X, y_loc, tw, 20),
                 GColorWhite, GTextAlignmentLeft);

  // Current temperature, extra-large, tucked close under the location.
  snprintf(buf, sizeof(buf), "%d\xC2\xB0", s_wx.temp);
  draw_text_halo(ctx, buf, s_font_xl, GRect(X, y_temp, tw, 56),
                 temp_color(s_wx.temp, s_wx.is_metric), GTextAlignmentLeft);

  // Condition summary under the temperature.
  draw_text_halo(ctx, wx_summary(s_wx.code), s_font_text,
                 GRect(X, y_sum, tw, 22), GColorWhite, GTextAlignmentLeft);

  // Hi / lo dataline below the summary.
  if (s_wx.updated == 0) {
    draw_text_halo(ctx, "no data", s_font_small, GRect(X, y_hilo + 1, tw, 16),
                   GColorLightGray, GTextAlignmentLeft);
  } else {
    snprintf(buf, sizeof(buf), "%d\xC2\xB0 / %d\xC2\xB0", s_wx.tmax, s_wx.tmin);
    draw_text_halo(ctx, buf, s_font_text, GRect(X, y_hilo, tw, 20),
                   GColorWhite, GTextAlignmentLeft);
  }

  // Warning pill over the scene, left-aligned, only for severe conditions.
  const char *warn = wx_warning(s_wx.code);
  if (warn) {
    GSize sz = graphics_text_layout_get_content_size(warn, s_font_small,
                 GRect(0, 0, tw, 20), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
    int pw = sz.w + 18, ph = 18, pyy = y_hilo + 25;   // 6px under the hi/lo row
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, GRect(X, pyy, pw, ph), ph / 2, GCornersAll);
    draw_text_in(ctx, warn, s_font_small, GRect(X, pyy + 1, pw, 16),
                 warning_color(s_wx.code), GTextAlignmentCenter);
  }
}

// A thin precip bar along the bottom of a row, width proportional to pop, shown
// only when rain is worth a glance (>= 20%). One drawn bar, never a chart.
static void draw_pop_bar(GContext *ctx, GRect row, uint8_t pop) {
  if (pop < 20) return;
  int w = row.size.w * pop / 100;
  graphics_context_set_fill_color(ctx, GColorPictonBlue);
  graphics_fill_rect(ctx, GRect(row.origin.x, row.origin.y + row.size.h - 2, w, 2),
                     0, GCornerNone);
}

// ---- hourly screen: next 8 hours -------------------------------------------

static void draw_hourly(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  time_t now = time(NULL);
  int now_hour = localtime(&now)->tm_hour;
  bool h24 = clock_is_24h_style();
  int row_h = b.size.h / HOURS_N;
  char buf[8];

  // Temperature span across the visible hours drives the trend-bar lengths.
  int lo = 127, hi = -128;
  for (int i = 0; i < HOURS_N; i++) {
    int t = s_wx.hours[i].temp;
    if (t < lo) lo = t;
    if (t > hi) hi = t;
  }
  int range = (hi - lo) < 1 ? 1 : (hi - lo);
  int trackX = b.origin.x + 72;
  int trackW = b.size.w - trackX - 46;
  int dotr = 5;
  int usable = trackW - 2 * dotr;

  for (int i = 0; i < HOURS_N; i++) {
    GRect row = GRect(b.origin.x, b.origin.y + i * row_h, b.size.w, row_h);
    HourSlot *hs = &s_wx.hours[i];
    int hour = (now_hour + i) % 24;
    int cy = row.origin.y + row_h / 2;
    GColor tint = temp_color(hs->temp, s_wx.is_metric);

    // Hour label, 12/24h per the watch's system setting.
    if (h24)               snprintf(buf, sizeof(buf), "%02d", hour);
    else                   snprintf(buf, sizeof(buf), "%d%s", (hour % 12) ? (hour % 12) : 12,
                                    hour < 12 ? "a" : "p");
    draw_text_in(ctx, buf, s_font_text, GRect(row.origin.x + 6, row.origin.y, 28, row_h),
                 GColorWhite, GTextAlignmentLeft);

    // Condition glyph, night variant if that hour is after sunset / before sunrise.
    bool night = is_night_at(now + i * 3600);
    draw_text_in(ctx, wx_glyph(hs->code, night), s_font_wx_sm,
                 GRect(row.origin.x + 34, row.origin.y - 2, 34, row_h),
                 GColorWhite, GTextAlignmentCenter);

    // Trend track spans the day's min..max; a ramp-tinted dot marks this hour.
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    graphics_fill_rect(ctx, GRect(trackX, cy - 1, trackW, 2), 1, GCornersAll);
    int dotx = trackX + dotr + (hs->temp - lo) * usable / range;
    graphics_context_set_fill_color(ctx, tint);
    graphics_fill_circle(ctx, GPoint(dotx, cy), dotr);

    // Temperature, tinted by the ramp.
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", hs->temp);
    draw_text_in(ctx, buf, s_font_text, GRect(row.origin.x, row.origin.y, row.size.w - 6, row_h),
                 tint, GTextAlignmentRight);

    draw_pop_bar(ctx, row, hs->pop);
  }
}

// ---- daily screen: next 6 days ---------------------------------------------

static void draw_daily(GContext *ctx, GRect b) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  time_t now = time(NULL);
  int row_h = b.size.h / DAYS_N;
  char buf[8];

  for (int i = 0; i < DAYS_N; i++) {
    GRect row = GRect(b.origin.x, b.origin.y + i * row_h, b.size.w, row_h);
    DaySlot *ds = &s_wx.days[i];

    // Weekday, three letters, derived from today plus the slot index.
    time_t day = now + i * 86400;
    strftime(buf, sizeof(buf), "%a", localtime(&day));
    draw_text_in(ctx, buf, s_font_text, GRect(row.origin.x + 6, row.origin.y, 52, row_h),
                 GColorWhite, GTextAlignmentLeft);

    // Condition glyph.
    draw_text_in(ctx, wx_glyph(ds->code, false), s_font_wx_sm,
                 GRect(row.origin.x + row.size.w / 2 - 20, row.origin.y - 2, 40, row_h),
                 GColorWhite, GTextAlignmentCenter);

    // Max at the right edge, min just left of it, each tinted by the ramp so a
    // cold day reads blue and a hot day red without any label.
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", ds->max);
    draw_text_in(ctx, buf, s_font_text, GRect(row.origin.x, row.origin.y, row.size.w - 8, row_h),
                 temp_color(ds->max, s_wx.is_metric), GTextAlignmentRight);
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", ds->min);
    draw_text_in(ctx, buf, s_font_text, GRect(row.origin.x, row.origin.y, row.size.w - 52, row_h),
                 temp_color(ds->min, s_wx.is_metric), GTextAlignmentRight);

    draw_pop_bar(ctx, row, ds->pop);
  }
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  switch (s_screen) {
    case SCREEN_HERO:   draw_hero(ctx, b); break;
    case SCREEN_HOURLY: draw_hourly(ctx, b); break;
    case SCREEN_DAILY:  draw_daily(ctx, b); break;
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
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_FEELS)))   s_wx.feels_like  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_WIND)))    s_wx.wind_speed  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_WDIR)))    s_wx.wind_dir    = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_GUST)))    s_wx.wind_gust   = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_PRECIP)))  s_wx.precip_mm   = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_STORM)))   s_wx.storm_prob  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_UV)))      s_wx.uv          = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_UVPEAK)))  s_wx.uv_peak     = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_AQI)))     s_wx.aqi         = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_MOON)))    s_wx.moon_phase  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_MOONILL))) s_wx.moon_illum  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_ALEVEL)))  s_wx.alert_level = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_ALABEL)))  s_wx.alert_label = tp->value->int32;
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
  s_font_xl     = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_AURA_48));
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
  app_message_open(768, 64);   // inbox holds the fuller current frame (name + 25 ints)
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
  fonts_unload_custom_font(s_font_xl);
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
