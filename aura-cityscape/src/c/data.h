#pragma once
#include <pebble.h>
#include "weather.h"

// Data layer: the Weather struct, persistence, the AppMessage inbox from the phone
// bridge, and a scripted MOCK mode (build-time, off in the shipping build) so the
// scenes can be previewed without a phone. The UI lives in aura-cityscape.c.

#define MOCK_SCENARIOS 8

typedef enum { PH_NIGHT = 0, PH_DAWN, PH_DAY, PH_GOLDEN, PH_DUSK } SkyPhase;

// Phase windows, in minutes around the sun times (set by the draft before init).
typedef struct { int dawn_before, dawn_after, golden_before, dusk_after; } PhaseCfg;

typedef struct {
  bool night;         // true between sunset and the next sunrise
  int  frac_1000;     // progress through the day (or the night), 0..1000
  int  alt_1000;      // sun altitude proxy: sin(pi*frac), 0..1000 (moon's arc at night)
} SunPos;

extern Weather g_wx;

// `change_cb` fires whenever new data lands (or the mock scenario changes).
void data_init(bool mock, int scenario, PhaseCfg cfg, void (*change_cb)(void));
void data_deinit(void);

time_t      data_now(void);                    // the real clock, or the scenario's fake one
bool        data_is_mock(void);
const char *data_mock_name(void);
void        data_mock_cycle(int dir);
void        data_request_refresh(void);

SkyPhase    data_phase_at(time_t t);           // phase of the sky at wall time t
SunPos      data_sun_pos(time_t t);
bool        data_night_at(time_t t);

const char *wx_summary(uint8_t code);
void        fmt_hhmm(char *buf, size_t n, time_t t);
GColor      temp_ink(int temp, bool is_metric);  // temp_color, lifted so it reads on dark
