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
