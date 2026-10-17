#include "data.h"
#include "message_keys.auto.h"

#define PKEY_WEATHER 1

Weather g_wx;

static bool       s_mock;
static int        s_scn;
static PhaseCfg   s_cfg;
static void     (*s_cb)(void);

// ---- scripted scenarios ----------------------------------------------------
// One row per preview state. Sun times are minutes after local midnight.
typedef struct {
  const char *name;
  int  minute;                 // fake "now"
  int  sr, ss;                 // sunrise / sunset minute
  uint8_t code, next_code;     // now, and from the 4th forecast hour on
  int8_t temp, tmin, tmax;
  uint8_t pop, wind, gust, hum, uv, uvpeak, illum, phase;
} Scn;

static const Scn SCN[MOCK_SCENARIOS] = {
  { "clear day",     13 * 60 + 10, 450, 1185, WX_CLEAR,    WX_FEW,     23, 11, 26,  0, 11, 18, 38, 7, 8, 55, 3 },
  { "golden hour",   19 * 60 + 5, 450, 1185, WX_FEW,      WX_CLEAR,   19, 11, 24,  5, 14, 21, 45, 2, 8, 55, 3 },
  { "rainy dusk",    19 * 60 + 55, 450, 1185, WX_RAIN,     WX_DRIZZLE, 14,  9, 17, 80, 22, 35, 82, 0, 4, 30, 6 },
  { "clear night",   23 * 60 + 30, 450, 1185, WX_CLEAR,    WX_CLEAR,    9,  6, 20,  0,  8, 12, 52, 0, 8, 78, 5 },
  { "snow",          11 * 60 + 0,  490, 1090, WX_SNOW,     WX_SNOW,    -2, -5,  1, 70, 12, 20, 88, 1, 2, 12, 1 },
  { "storm",         17 * 60 + 30, 450, 1185, WX_THUNDER,  WX_HEAVY,   24, 18, 28, 90, 31, 52, 80, 1, 5, 40, 2 },
  { "dawn fog",       7 * 60 + 50, 450, 1185, WX_FOG,      WX_FEW,      8,  5, 19, 10,  6,  4, 96, 0, 6, 92, 4 },
  { "overcast",      15 * 60 + 0,  450, 1185, WX_OVERCAST, WX_CLOUDY,  17, 13, 19, 20, 18, 28, 70, 2, 3, 70, 3 },
};

static time_t midnight_today(void) {
  time_t t = time(NULL);
  struct tm lt = *localtime(&t);
  lt.tm_hour = 0; lt.tm_min = 0; lt.tm_sec = 0;
  return mktime(&lt);
}

static void load_scenario(int i) {
  const Scn *s = &SCN[i];
  memset(&g_wx, 0, sizeof(g_wx));
  strncpy(g_wx.name, "Madrid", sizeof(g_wx.name) - 1);
  g_wx.temp = s->temp; g_wx.tmin = s->tmin; g_wx.tmax = s->tmax;
  g_wx.code = s->code; g_wx.pop = s->pop;
  g_wx.is_metric = 1;
  time_t m0 = midnight_today();
  g_wx.sunrise = m0 + s->sr * 60; g_wx.sunset = m0 + s->ss * 60;
  g_wx.wind_speed = s->wind;
  g_wx.moon_illum = s->illum; g_wx.moon_phase = s->phase;
  g_wx.updated = m0 + s->minute * 60;
  static const int8_t HOFF[HOURS_N] = { 0, 1, 1, 0, -1, -2, -3, -3 };
  for (int h = 0; h < HOURS_N; h++) {
    g_wx.hours[h].temp = s->temp + HOFF[h];
    g_wx.hours[h].code = h < 3 ? s->code : s->next_code;
    g_wx.hours[h].pop  = h < 3 ? s->pop : s->pop / 2;
  }
  static const uint8_t DCODE[DAYS_N] = { WX_FEW, WX_RAIN, WX_CLOUDY, WX_CLEAR, WX_SNOW, WX_THUNDER };
  static const int8_t DOFF[DAYS_N]  = { 1, -3, -2, 2, 3, 0 };
  for (int d = 0; d < DAYS_N; d++) {
    g_wx.days[d].code = d == 0 ? s->next_code : DCODE[d];
    g_wx.days[d].min = s->tmin + DOFF[d];
    g_wx.days[d].max = s->tmax + DOFF[d] - (d == 4 ? 3 : 0);
    g_wx.days[d].pop = DCODE[d] >= WX_DRIZZLE ? 60 : 10;
  }
}

// ---- clock + sun -----------------------------------------------------------

bool data_is_mock(void) { return s_mock; }
const char *data_mock_name(void) { return SCN[s_scn].name; }

time_t data_now(void) {
  if (!s_mock) return time(NULL);
  return midnight_today() + SCN[s_scn].minute * 60;
}

void data_mock_cycle(int dir) {
  if (!s_mock) return;
  s_scn = (s_scn + dir + MOCK_SCENARIOS) % MOCK_SCENARIOS;
  load_scenario(s_scn);
  if (s_cb) s_cb();
}

// Sunrise and sunset of the day that contains `t`, by whole-day shifts of the pair the
// phone sent (the day length changes by minutes per day, so a few days of drift is
// invisible). Pure epoch arithmetic, so it holds for a city in another timezone than
// the watch: the phone's pair is in absolute time, and nothing here uses local time.
// On return sr <= t < sr + 24 h.
static bool sun_times(time_t t, time_t *sr, time_t *ss) {
  if (g_wx.sunrise == 0 || g_wx.sunset == 0 || g_wx.sunset <= g_wx.sunrise) return false;
  int64_t d = (int64_t)t - g_wx.sunrise;
  int64_t k = d >= 0 ? d / 86400 : -((-d + 86399) / 86400);   // floor
  *sr = (time_t)(g_wx.sunrise + k * 86400);
  *ss = (time_t)(g_wx.sunset + k * 86400);
  return true;
}

bool data_night_at(time_t t) {
  time_t sr, ss;
  if (!sun_times(t, &sr, &ss)) return false;
  return t > ss;                               // sr <= t by construction
}

SkyPhase data_phase_at(time_t t) {
  time_t sr, ss;
  if (!sun_times(t, &sr, &ss)) return PH_DAY;
  int m = 60;
  if (t >= sr - s_cfg.dawn_before * m && t < sr + s_cfg.dawn_after * m) return PH_DAWN;
  if (t >= ss - s_cfg.golden_before * m && t <= ss) return PH_GOLDEN;
  if (t > ss && t <= ss + s_cfg.dusk_after * m) return PH_DUSK;
  if (t >= sr && t < ss) return PH_DAY;
  if (t >= sr + 86400 - s_cfg.dawn_before * m) return PH_DAWN;   // the next dawn, just before sunrise
  return PH_NIGHT;
}

static int32_t sin_pi_1000(int32_t f) {
  if (f < 0) f = 0;
  if (f > 1000) f = 1000;
  int32_t s = sin_lookup((TRIG_MAX_ANGLE / 2) * f / 1000);
  if (s < 0) s = 0;
  return s * 1000 / TRIG_MAX_RATIO;
}

SunPos data_sun_pos(time_t t) {
  SunPos p = { false, 500, 1000 };
  time_t sr, ss;
  if (!sun_times(t, &sr, &ss)) return p;
  if (t <= ss) {
    p.frac_1000 = (int)((int64_t)(t - sr) * 1000 / (ss - sr));
  } else {
    p.night = true;
    time_t nr = sr + 86400;                    // the night runs from this sunset to the next sunrise
    p.frac_1000 = (int)((int64_t)(t - ss) * 1000 / (nr - ss));
  }
  p.alt_1000 = sin_pi_1000(p.frac_1000);
  return p;
}

// ---- colours, glyphs, strings ---------------------------------------------

GColor temp_color(int temp, bool is_metric) {
  int c = is_metric ? temp : (temp - 32) * 5 / 9;
  if (c < -4) return GColorIndigo;
  if (c <  2) return GColorLiberty;
  if (c <  8) return GColorBlueMoon;
  if (c < 13) return GColorTiffanyBlue;
  if (c < 17) return GColorMalachite;
  if (c < 20) return GColorGreen;
  if (c < 23) return GColorInchworm;
  if (c < 26) return GColorYellow;
  if (c < 29) return GColorChromeYellow;
  if (c < 32) return GColorOrange;
  if (c < 35) return GColorSunsetOrange;
  if (c < 39) return GColorRed;
  if (c < 43) return GColorDarkCandyAppleRed;
  return GColorBulgarianRose;
}

GColor temp_ink(int temp, bool is_metric) {
  GColor c = temp_color(temp, is_metric);
  if (gcolor_equal(c, GColorBulgarianRose))     return GColorSunsetOrange;
  if (gcolor_equal(c, GColorDarkCandyAppleRed)) return GColorRed;
  if (gcolor_equal(c, GColorIndigo))            return GColorBlueMoon;
  if (gcolor_equal(c, GColorLiberty))           return GColorBlueMoon;
  return c;
}

const char *wx_glyph(uint8_t code, bool night) {
  switch (code) {
    case WX_CLEAR:    return night ? "\xEF\x80\xAE" : "\xEF\x80\x8D";
    case WX_FEW:      return night ? "\xEF\x82\x86" : "\xEF\x80\x82";
    case WX_CLOUDY:   return "\xEF\x81\x81";
    case WX_OVERCAST: return "\xEF\x80\x93";
    case WX_FOG:      return "\xEF\x80\x94";
    case WX_DRIZZLE:  return "\xEF\x80\x9C";
    case WX_RAIN:     return "\xEF\x80\x99";
    case WX_HEAVY:    return "\xEF\x80\x98";
    case WX_SNOW:     return "\xEF\x80\x9B";
    case WX_THUNDER:  return "\xEF\x80\x9E";
    default:          return "\xEF\x80\x82";
  }
}

const char *wx_summary(uint8_t code) {
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
    case WX_THUNDER:  return "Storm";
    default:          return "";
  }
}

void fmt_hhmm(char *buf, size_t n, time_t t) {
  strftime(buf, n, clock_is_24h_style() ? "%H:%M" : "%I:%M", localtime(&t));
}

// ---- persistence + phone bridge -------------------------------------------

static uint8_t clamp_code(int v) { return (v < 0 || v > WX_THUNDER) ? WX_CLEAR : (uint8_t)v; }

// Never trust a stored or received blob: clamp every enum and terminate the name,
// so a corrupt or older persisted struct can only look odd, never index out of range.
static void sanitise(void) {
  g_wx.name[sizeof(g_wx.name) - 1] = 0;
  g_wx.code = clamp_code(g_wx.code);
  for (int i = 0; i < HOURS_N; i++) g_wx.hours[i].code = clamp_code(g_wx.hours[i].code);
  for (int i = 0; i < DAYS_N; i++)  g_wx.days[i].code  = clamp_code(g_wx.days[i].code);
  if (g_wx.moon_phase > 7)   g_wx.moon_phase = 0;
  if (g_wx.moon_illum > 100) g_wx.moon_illum = 100;
  if (g_wx.pop > 100)        g_wx.pop = 100;
}

// Fresh install: no forecast yet (updated == 0). The UI shows the "no forecast"
// state instead of made-up numbers; the sun times only drive the sky behind it.
static void seed_default(void) {
  memset(&g_wx, 0, sizeof(g_wx));
  strncpy(g_wx.name, "Aura Cityscape", sizeof(g_wx.name) - 1);
  g_wx.code = WX_CLEAR; g_wx.is_metric = 1;
  time_t m0 = midnight_today();
  g_wx.sunrise = m0 + 7 * 3600; g_wx.sunset = m0 + 21 * 3600;
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  if (s_mock) return;
  Tuple *t, *tp;
  if ((t = dict_find(iter, MESSAGE_KEY_WX_OK))) {
    if (t->value->int32 == 0) return;          // phone could not fetch: keep what we have
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_NAME))) {
      strncpy(g_wx.name, tp->value->cstring, sizeof(g_wx.name) - 1);
      g_wx.name[sizeof(g_wx.name) - 1] = 0;
    }
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_TEMP)))    g_wx.temp     = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_TMIN)))    g_wx.tmin     = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_TMAX)))    g_wx.tmax     = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_CODE)))    g_wx.code     = clamp_code(tp->value->int32);
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_POP)))     g_wx.pop      = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_SUNRISE))) g_wx.sunrise  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_SUNSET)))  g_wx.sunset   = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_UNITS)))   g_wx.is_metric = tp->value->int32 ? 1 : 0;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_UPDATED))) g_wx.updated  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_WIND)))    g_wx.wind_speed  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_MOON)))    g_wx.moon_phase  = tp->value->int32;
    if ((tp = dict_find(iter, MESSAGE_KEY_WX_MOONILL))) g_wx.moon_illum  = tp->value->int32;
    sanitise();
    if (s_cb) s_cb();
    return;
  }
  if ((t = dict_find(iter, MESSAGE_KEY_H_IDX))) {
    int i = t->value->int32;
    if (i >= 0 && i < HOURS_N) {
      if ((tp = dict_find(iter, MESSAGE_KEY_H_TEMP))) g_wx.hours[i].temp = tp->value->int32;
      if ((tp = dict_find(iter, MESSAGE_KEY_H_CODE))) g_wx.hours[i].code = clamp_code(tp->value->int32);
      if ((tp = dict_find(iter, MESSAGE_KEY_H_POP)))  g_wx.hours[i].pop  = tp->value->int32;
    }
    return;                                    // drawn once the last daily frame lands
  }
  if ((t = dict_find(iter, MESSAGE_KEY_D_IDX))) {
    int i = t->value->int32;
    if (i >= 0 && i < DAYS_N) {
      if ((tp = dict_find(iter, MESSAGE_KEY_D_MIN)))  g_wx.days[i].min  = tp->value->int32;
      if ((tp = dict_find(iter, MESSAGE_KEY_D_MAX)))  g_wx.days[i].max  = tp->value->int32;
      if ((tp = dict_find(iter, MESSAGE_KEY_D_CODE))) g_wx.days[i].code = clamp_code(tp->value->int32);
      if ((tp = dict_find(iter, MESSAGE_KEY_D_POP)))  g_wx.days[i].pop  = tp->value->int32;
    }
    // The last daily frame completes a forecast: store the whole thing in one write.
    if (i == DAYS_N - 1) {
      persist_write_data(PKEY_WEATHER, &g_wx, sizeof(g_wx));
      if (s_cb) s_cb();
    }
    return;
  }
}

void data_request_refresh(void) {
  if (s_mock) return;
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) == APP_MSG_OK) {
    dict_write_uint8(out, MESSAGE_KEY_REFRESH, 1);
    app_message_outbox_send();
  }
}

// The phone sends one frame at a time: the biggest is the current-conditions frame
// (13 tuples, about 150 bytes with the name). 256 in leaves headroom; 32 out is
// plenty for the one-byte REFRESH request.
#define INBOX_SIZE  256
#define OUTBOX_SIZE  32

void data_init(bool mock, int scenario, PhaseCfg cfg, void (*change_cb)(void)) {
  s_mock = mock; s_cfg = cfg; s_cb = change_cb;
  s_scn = (scenario % MOCK_SCENARIOS + MOCK_SCENARIOS) % MOCK_SCENARIOS;
  if (mock) {
    load_scenario(s_scn);
  } else if (persist_exists(PKEY_WEATHER) &&
             persist_get_size(PKEY_WEATHER) == (int)sizeof(g_wx)) {
    persist_read_data(PKEY_WEATHER, &g_wx, sizeof(g_wx));
    sanitise();
  } else {
    seed_default();
  }
  app_message_register_inbox_received(inbox_received);
  app_message_open(INBOX_SIZE, OUTBOX_SIZE);
}

void data_deinit(void) {}
