#pragma once
#include <pebble.h>

// The on-watch data model. The watch never parses a forecast; the phone sends a
// compact, already-digested struct (see docs/01-data-bridge.md) and the watch
// only formats and tints it. Everything is kept as small integers.

#define HOURS_N 8
#define DAYS_N  6

// Normalised condition enum. The phone maps every provider's own taxonomy
// (WMO integers, AEMET "estado del cielo" strings) into exactly these ten
// values before sending, so the watch understands one code set and no more.
enum {
  WX_CLEAR    = 0,
  WX_FEW      = 1,   // few clouds
  WX_CLOUDY   = 2,
  WX_OVERCAST = 3,
  WX_FOG      = 4,
  WX_DRIZZLE  = 5,
  WX_RAIN     = 6,
  WX_HEAVY    = 7,   // heavy rain
  WX_SNOW     = 8,
  WX_THUNDER  = 9,
};

// Aviso phenomenon, one shared enum for the derived worldwide advisory and the
// official AEMET aviso in Spain. The phone maps its thresholds (and AEMET's
// phenomena) into these; the watch only picks the word and the ramp colour.
enum {
  ALERT_NONE  = 0,
  ALERT_HEAT  = 1,
  ALERT_STORM = 2,
  ALERT_SNOW  = 3,
  ALERT_WIND  = 4,
  ALERT_FOG   = 5,
  ALERT_RAIN  = 6,
  ALERT_COLD  = 7,
};

typedef struct { int8_t temp; uint8_t code; uint8_t pop; } HourSlot;              // pop = precip probability %
typedef struct { int8_t min; int8_t max; uint8_t code; uint8_t pop; } DaySlot;

typedef struct {
  char     name[24];         // location label, e.g. "Madrid"
  int8_t   temp;             // current temp, whole degrees (display units)
  int8_t   tmin, tmax;       // today hi/lo
  uint8_t  code;             // normalised condition (enum above)
  uint8_t  humidity;         // %
  uint8_t  pop;              // current precip probability %
  int32_t  sunrise, sunset;  // unix seconds, today; the watch draws the sun from these
  uint8_t  is_metric;        // 0 = F, 1 = C

  // Current conditions the hero omits, for the wind and details cards (Phase A).
  int8_t   feels_like;       // apparent temperature, display units
  uint8_t  wind_speed;       // km/h (metric) or mph (imperial); the phone converts
  uint8_t  wind_dir;         // 16-point index 0-15, direction the wind blows FROM
  uint8_t  wind_gust;        // same units as wind_speed
  uint8_t  precip_mm;        // whole mm, capped at 255
  uint8_t  storm_prob;       // thunderstorm probability %

  // Environment (Phase A). aqi is Spain-only (MITECO), 0 = no station near.
  uint8_t  uv;               // current UV index
  uint8_t  uv_peak;          // today's peak UV, the ring's full scale
  uint8_t  aqi;              // ICA category 1-6, 0 = none

  // Astronomy (Phase A), computed on the phone.
  uint8_t  moon_phase;       // 0-7 principal phase (0 new .. 4 full .. 7 waning crescent)
  uint8_t  moon_illum;       // illuminated fraction %

  // Aviso (Phase A). level 0 none / 1 verde / 2 amarillo / 3 naranja / 4 rojo;
  // label indexes the ALERT_* enum. Derived worldwide, official AEMET in Spain.
  uint8_t  alert_level;
  uint8_t  alert_label;

  HourSlot hours[HOURS_N];
  DaySlot  days[DAYS_N];
  int32_t  updated;          // unix seconds of the fetch (0 = no data yet)
} Weather;

// Temperature ramp from docs/PALETTE.md, snapped to the 64-colour Emery palette.
// `temp` is in display units; `is_metric` lets it tint consistently across C/F.
GColor temp_color(int temp, bool is_metric);

// Weather Icons glyph (a PUA codepoint string) for a normalised code. `night`
// swaps the clear/few-clouds day glyphs for their moon variants, matching the
// hero: the watch derives night from the clock vs sun times, never a code suffix.
const char *wx_glyph(uint8_t code, bool night);
