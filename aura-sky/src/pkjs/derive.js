// Phone-side derivation for Aura Sky Weather: the moon phase and illuminated
// fraction, computed from the date, so the night sky can draw the real moon.
// Both providers return the same normalised object; index.js enriches it once
// after the fetch, before the frames are sent.

// Reference new moon: 2000-01-06 18:14 UTC. Synodic month in ms.
var LUNAR_EPOCH = 947182440000;
var SYNODIC_MS = 29.530588853 * 86400000;

function moon(nowMs) {
  var age = ((nowMs - LUNAR_EPOCH) % SYNODIC_MS + SYNODIC_MS) % SYNODIC_MS;
  var frac = age / SYNODIC_MS;                          // 0 new .. 0.5 full .. 1 new
  var illum = Math.round(50 * (1 - Math.cos(2 * Math.PI * frac)));
  var phase = Math.round(frac * 8) % 8;                 // 0-7 principal phase
  return { phase: phase, illum: illum };
}

function enrich(w) {
  var m = moon(Date.now());
  w.moon_phase = m.phase;
  w.moon_illum = m.illum;
  return w;
}

module.exports = { enrich: enrich };
