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
var WX = { FOG: 4, HEAVY: 7, SNOW: 8, THUNDER: 9 };

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

// Fill moon_* always, and the derived aviso only when the provider has not
// already set an official one (alert_level > 0 means AEMET filled it in Spain).
function enrich(w) {
  var m = moon(Date.now());
  w.moon_phase = m.phase;
  w.moon_illum = m.illum;
  if (!w.alert_level) {
    var a = alert(w);
    w.alert_level = a.level;
    w.alert_label = a.label;
  }
  return w;
}

module.exports = { enrich: enrich, ALERT: ALERT };
