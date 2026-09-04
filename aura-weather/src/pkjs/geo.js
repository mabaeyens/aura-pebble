// Spain -> AEMET municipality resolution (docs/02). AEMET has no lat/lon
// endpoint, so a Spanish pick must carry an INE code. This runs on the phone,
// where the 8122-entry table has room the watch does not. The worldwide
// typeahead itself lives inline in the config page (config.js); this file only
// turns a coordinate into the nearest INE, the port of MunicipioDatabase.nearest.

var MUNI = require('./municipios.min.json');

// Rough Spain bounding boxes: mainland + Balearics, and the Canaries. A cheap
// pre-check so a non-Spanish fix skips the full 8122-entry scan entirely.
function inSpain(lat, lon) {
  if (lat >= 27.5 && lat <= 29.5 && lon >= -18.3 && lon <= -13.3) return true;  // Canaries
  if (lat >= 35.9 && lat <= 43.9 && lon >= -9.4 && lon <= 4.5) return true;     // mainland + Balearics
  return false;
}

// Nearest municipality's INE by minimum distance, or '' if the point is not in
// Spain. Squared equirectangular distance (longitude scaled by cos lat) is
// monotonic with great-circle distance at this scale, so it ranks identically
// while avoiding a trig call per entry.
function nearestINE(lat, lon) {
  if (!inSpain(lat, lon)) return '';
  var kx = Math.cos(lat * Math.PI / 180);
  var best = '', bestD = 1e18;
  for (var i = 0; i < MUNI.length; i++) {
    var m = MUNI[i];
    var dlat = m.lat - lat, dlon = (m.lon - lon) * kx;
    var d = dlat * dlat + dlon * dlon;
    if (d < bestD) { bestD = d; best = m.ine; }
  }
  return best;
}

// AEMET autonomous-community code by INE province (the first two digits of the
// municipal INE code), for the official boletín endpoint (docs/06 Phase C). Index
// 1-52 = province number; the value is AEMET's ccaa parameter. Best-effort: a
// wrong code just fails the regional fetch, which falls back to the national
// boletín and then to the generated bulletin, so it degrades safely.
var CCAA_BY_PROV = [
  '',                                                        // 0 unused
  'pva', 'clm', 'val', 'and', 'cle', 'ext', 'bal', 'cat',    // 01-08
  'cle', 'ext', 'and', 'val', 'clm', 'and', 'gal', 'clm',    // 09-16
  'cat', 'and', 'clm', 'pva', 'and', 'ara', 'and', 'cle',    // 17-24
  'cat', 'rio', 'gal', 'mad', 'and', 'mur', 'nav', 'gal',    // 25-32
  'ast', 'cle', 'can', 'gal', 'cle', 'can', 'cnt', 'cle',    // 33-40
  'and', 'cle', 'cat', 'ara', 'clm', 'val', 'cle', 'pva',    // 41-48
  'cle', 'ara', 'ceu', 'mel',                                // 49-52
];

function ccaaForINE(ine) {
  var p = parseInt(String(ine || '').slice(0, 2), 10);
  return (p >= 1 && p <= 52) ? CCAA_BY_PROV[p] : '';
}

module.exports = { inSpain: inSpain, nearestINE: nearestINE, ccaaForINE: ccaaForINE };
