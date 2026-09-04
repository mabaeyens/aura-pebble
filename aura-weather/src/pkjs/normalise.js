// Normalisation: every provider's own taxonomy collapses to the watch's tiny
// 10-value code set here, and units are converted once on the phone. The watch
// must never see a raw WMO or AEMET code. See docs/01-data-bridge.md.

// WMO weather_code (Open-Meteo) -> normalised code.
function wmoToCode(w) {
  if (w === 0) return 0;                                   // clear
  if (w === 1 || w === 2) return 1;                        // few clouds
  if (w === 3) return 3;                                   // overcast
  if (w === 45 || w === 48) return 4;                      // fog
  if ([51, 53, 55, 56, 57].indexOf(w) >= 0) return 5;      // drizzle
  if ([61, 63, 80, 81].indexOf(w) >= 0) return 6;          // rain
  if ([65, 66, 67, 82].indexOf(w) >= 0) return 7;          // heavy rain
  if ([71, 73, 75, 77, 85, 86].indexOf(w) >= 0) return 8;  // snow
  if ([95, 96, 99].indexOf(w) >= 0) return 9;              // thunder
  return 1;
}

// AEMET "estado del cielo" code (a string, optional trailing 'n' for night)
// -> normalised code. Night is derived on the watch from sun times, so the
// suffix is stripped and ignored here. Built for step 5; unused until then.
function aemetToCode(s) {
  var n = parseInt(String(s).replace(/n$/, ''), 10);
  if (n === 11) return 0;
  if (n === 12 || n === 13) return 1;
  if (n === 14 || n === 15) return 2;
  if (n === 16 || n === 17) return 3;
  if (n === 81 || n === 82) return 4;
  if (n === 23 || n === 43) return 5;
  if ([24, 44].indexOf(n) >= 0) return 6;
  if ([25, 26, 45, 46].indexOf(n) >= 0) return 7;
  if ([33, 34, 35, 36, 71, 72, 73, 74].indexOf(n) >= 0) return 8;
  if ([51, 52, 53, 54, 61, 62, 63, 64].indexOf(n) >= 0) return 9;
  return 1;
}

// Clamp to the int8 the watch stores, rounding to a whole degree.
function i8(x) {
  var v = Math.round(x);
  if (v > 127) v = 127;
  if (v < -128) v = -128;
  return v;
}

function u8(x) {
  var v = Math.round(x);
  if (v > 255) v = 255;
  if (v < 0) v = 0;
  return v;
}

// Compass bearing in degrees -> 16-point index 0-15 (0 = N, clockwise). The
// watch turns the index back into a needle angle and a cardinal label.
function dir16(deg) {
  var d = Number(deg);
  if (isNaN(d)) return 0;
  return (Math.round(((d % 360) + 360) % 360 / 22.5)) % 16;
}

// AEMET reports wind direction as a Spanish 8/16-point string ("N", "NE",
// "SO", "NNO", "C" for calm) -> the same 16-point index.
var AEMET_DIRS = {
  N: 0, NNE: 1, NE: 2, ENE: 3, E: 4, ESE: 5, SE: 6, SSE: 7,
  S: 8, SSO: 9, SO: 10, OSO: 11, O: 12, ONO: 13, NO: 14, NNO: 15, C: 0,
};
function dirAemet(s) {
  var k = String(s || '').toUpperCase().trim();
  return AEMET_DIRS[k] || 0;
}

// European AQI (0..100+) -> the 1-6 air-quality band the watch stores, which
// lines up one-for-one with Spain's ICA categories (Good, Fair, Moderate, Poor,
// Very poor, Extremely poor). null/absent -> 0 = no data, so no card shows; a
// real 0 reading is clean air and still bands to 1.
function aqiBand(v) {
  if (v === null || v === undefined) return 0;
  var n = Number(v);
  if (isNaN(n) || n < 0) return 0;
  if (n <= 20)  return 1;
  if (n <= 40)  return 2;
  if (n <= 60)  return 3;
  if (n <= 80)  return 4;
  if (n <= 100) return 5;
  return 6;
}

module.exports = {
  wmoToCode: wmoToCode, aemetToCode: aemetToCode, i8: i8, u8: u8,
  dir16: dir16, dirAemet: dirAemet, aqiBand: aqiBand,
};
