// Phone-side derivation: the words and warnings Aura makes out of numbers, so no
// wearer ever sees an empty card (docs/06). Two things live here:
//   - the moon phase and illumination, computed from the date, and
//   - the threshold aviso: a coarse advisory derived from the current numbers,
//     the worldwide floor that AEMET's official CAP aviso overwrites in Spain.
// Both providers return the same normalised object, so this enriches it once in
// index.js after the fetch, before the frames are sent.

// Alert labels mirror the ALERT_* enum in weather.h.
var ALERT = { NONE: 0, HEAT: 1, STORM: 2, SNOW: 3, WIND: 4, FOG: 5, RAIN: 6, COLD: 7 };
// Condition codes mirror the WX_* enum in weather.h.
var WX = { CLEAR: 0, FEW: 1, CLOUDY: 2, OVERCAST: 3, FOG: 4, DRIZZLE: 5,
           RAIN: 6, HEAVY: 7, SNOW: 8, THUNDER: 9 };

// Plain-language condition words, indexed by the normalised code, for the
// generated bulletin (the hero's one-word summary is on the watch).
var COND = ['Clear', 'A few clouds', 'Cloudy', 'Overcast', 'Fog', 'Drizzle',
            'Rain', 'Heavy rain', 'Snow', 'Thunderstorms'];
// 16-point compass, matching the watch's dir16 (index = direction FROM).
var DIR = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE',
           'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];

// Reference new moon: 2000-01-06 18:14 UTC. Synodic month in days.
var LUNAR_EPOCH = 947182440000;   // ms
var SYNODIC_MS = 29.530588853 * 86400000;

function moon(nowMs) {
  var age = ((nowMs - LUNAR_EPOCH) % SYNODIC_MS + SYNODIC_MS) % SYNODIC_MS;
  var frac = age / SYNODIC_MS;                          // 0 new .. 0.5 full .. 1 new
  var illum = Math.round(50 * (1 - Math.cos(2 * Math.PI * frac)));
  var phase = Math.round(frac * 8) % 8;                 // 0-7 principal phase
  return { phase: phase, illum: illum };
}

// Everything compares in Celsius and km/h; convert the display-unit fields back.
function toC(t, metric) { return metric ? t : (t - 32) * 5 / 9; }
function toKmh(v, metric) { return metric ? v : v * 1.609; }

// Highest-severity advisory from the current numbers. Returns {level, label},
// level 0 when nothing crosses a threshold. Severity leans conservative: a lone
// condition code is amarillo (2), an extreme reading is naranja (3).
function alert(w) {
  var metric = w.is_metric === 1;
  var feelsC = toC(w.feels_like, metric);
  var gust = toKmh(w.wind_gust, metric);
  var cands = [];
  if (w.code === WX.THUNDER)          cands.push({ level: gust >= 70 ? 3 : 2, label: ALERT.STORM });
  else if (w.storm_prob >= 60)        cands.push({ level: 2, label: ALERT.STORM });
  if (gust >= 90)                     cands.push({ level: 3, label: ALERT.WIND });
  else if (gust >= 70)                cands.push({ level: 2, label: ALERT.WIND });
  if (w.precip_mm >= 20)              cands.push({ level: 3, label: ALERT.RAIN });
  else if (w.precip_mm >= 10 || w.code === WX.HEAVY) cands.push({ level: 2, label: ALERT.RAIN });
  if (w.code === WX.SNOW)             cands.push({ level: 2, label: ALERT.SNOW });
  if (feelsC >= 40)                   cands.push({ level: 3, label: ALERT.HEAT });
  else if (feelsC >= 36)              cands.push({ level: 2, label: ALERT.HEAT });
  if (feelsC <= -12)                  cands.push({ level: 3, label: ALERT.COLD });
  else if (feelsC <= -6)              cands.push({ level: 2, label: ALERT.COLD });
  if (w.code === WX.FOG)              cands.push({ level: 1, label: ALERT.FOG });

  var best = { level: 0, label: ALERT.NONE };
  for (var i = 0; i < cands.length; i++) if (cands[i].level > best.level) best = cands[i];
  return best;
}

// How the current temperature feels, in Celsius bands, for the bulletin prose.
function feelWord(c) {
  if (c < 0)  return 'freezing';
  if (c < 8)  return 'cold';
  if (c < 16) return 'cool';
  if (c < 24) return 'mild';
  if (c < 31) return 'warm';
  return 'hot';
}
function windWord(kmh) {
  if (kmh < 12) return '';         // calm: no wind sentence
  if (kmh < 20) return 'A light';
  if (kmh < 39) return 'A moderate';
  if (kmh < 62) return 'A fresh';
  return 'A strong';
}

// Aura's own plain-language forecast, written from the numbers so every wearer
// gets words, not a bare grid (docs/06). This is the hero headline now, so it
// stays short and fits two lines: current condition and feel, then today's
// range. Wind reads as its own hero line, and rain/snow/storm/UV each have their
// own card or the aviso, so they are not repeated here. An official AEMET boletin
// (long) overrides this in Spain and keeps its own Forecast card.
function bulletin(w) {
  var metric = w.is_metric === 1;
  var tempC = toC(w.temp, metric);
  var deg = '°';
  return (COND[w.code] || 'Mixed') + ', ' + feelWord(tempC) + ' at ' + w.temp + deg
       + '. High ' + w.tmax + deg + ', low ' + w.tmin + deg + '.';
}

// Fill moon_* always; fill the derived aviso and the generated bulletin only when
// the provider has not already set an official one (a non-empty value means AEMET
// filled it in Spain, and an official reading always outranks a derived one).
function enrich(w) {
  var m = moon(Date.now());
  w.moon_phase = m.phase;
  w.moon_illum = m.illum;
  if (!w.alert_level) {
    var a = alert(w);
    w.alert_level = a.level;
    w.alert_label = a.label;
  }
  if (!w.bulletin) w.bulletin = bulletin(w);
  return w;
}

module.exports = { enrich: enrich, bulletin: bulletin, ALERT: ALERT };
