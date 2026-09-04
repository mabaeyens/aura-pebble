// AEMET official CAP aviso parsing (docs/06 Phase C, Spain). The avisos_cap
// endpoint hands back a tar archive of CAP XML files, one per active warning.
// This module does the two parts that parse cleanly in the JS sandbox and are
// unit-testable: untar the archive, and read each CAP into Aura's {level, label}.
// The binary transport (fetching the tar, and the gzip case) lives in
// providers.js and degrades to the derived aviso on any failure, so a wrong area
// code, a gzip payload or a parse slip never blanks or crashes the aviso card.

// Mirrors the ALERT_* enum in weather.h and derive.js.
var ALERT = { NONE: 0, HEAT: 1, STORM: 2, SNOW: 3, WIND: 4, FOG: 5, RAIN: 6, COLD: 7 };

// Untar a Uint8Array into [{name, text}]. Plain USTAR: 512-byte header (name at
// 0, octal size at 124), then the content padded to a 512-byte boundary, ending
// on a zero block. Content is decoded latin1 (one byte per char); the fields
// this reads (nivel colour, phenomenon, ISO times) are ASCII, so that is enough.
function untar(bytes) {
  var files = [], off = 0, n = bytes.length;
  function str(start, len) {
    var s = '';
    for (var i = start; i < start + len && i < n; i++) {
      var c = bytes[i];
      if (c === 0) break;
      s += String.fromCharCode(c);
    }
    return s;
  }
  while (off + 512 <= n) {
    var name = str(off, 100).replace(/\s+$/, '');
    if (!name) break;                                  // zero block: end of archive
    var size = parseInt(str(off + 124, 12).replace(/[^0-7]/g, ''), 8) || 0;
    var cstart = off + 512, text = '';
    for (var i = 0; i < size && cstart + i < n; i++) text += String.fromCharCode(bytes[cstart + i]);
    files.push({ name: name, text: text });
    off = cstart + Math.ceil(size / 512) * 512;
  }
  return files;
}

function tagVal(xml, tag) {
  var m = xml.match(new RegExp('<' + tag + '>([^<]*)</' + tag + '>', 'i'));
  return m ? m[1].trim() : '';
}

// An AEMET CAP <parameter> whose valueName contains `key` (e.g. "nivel",
// "fenomeno"): <valueName>AEMET-Meteoalerta nivel</valueName><value>naranja</value>.
function capParam(xml, key) {
  var m = xml.match(new RegExp('<valueName>[^<]*' + key + '[^<]*</valueName>\\s*<value>([^<]*)</value>', 'i'));
  return m ? m[1].trim() : '';
}

function isoMs(s) {
  if (!s) return 0;
  var t = Date.parse(s);
  return isNaN(t) ? 0 : t;
}

// AEMET phenomenon (Spanish) -> ALERT_*. Matched on ASCII fragments so accents
// (which latin1 decoding mangles) never matter. Unknown still flags a warning.
function fenLabel(f) {
  var s = (f || '').toLowerCase();
  if (s.indexOf('torment') >= 0) return ALERT.STORM;
  if (s.indexOf('nev') >= 0 || s.indexOf('alud') >= 0) return ALERT.SNOW;
  if (s.indexOf('viento') >= 0 || s.indexOf('costero') >= 0) return ALERT.WIND;
  if (s.indexOf('niebla') >= 0 || s.indexOf('calima') >= 0 || s.indexOf('polvo') >= 0) return ALERT.FOG;
  if (s.indexOf('lluvia') >= 0 || s.indexOf('precip') >= 0) return ALERT.RAIN;
  // "Temperaturas máximas/mínimas": latin1 mangles the accent, so split on the
  // 'x' that only máximas carries (heat) versus mínimas (cold).
  if (s.indexOf('temperatura') >= 0) return (s.indexOf('x') >= 0) ? ALERT.HEAT : ALERT.COLD;
  if (s.indexOf('calor') >= 0) return ALERT.HEAT;
  if (s.indexOf('fri') >= 0) return ALERT.COLD;
  return ALERT.STORM;
}

// One CAP document -> {level, label} if it is a real, currently-active warning,
// else null. Colour drives the level (verde/amarillo/naranja/rojo = 1/2/3/4);
// verde (1) is "no warning" so it is dropped. Outside its onset..expires window
// it is dropped too, but a missing/bad time is not held against it.
function capAlert(xml, nowMs) {
  var nivel = capParam(xml, 'nivel').toLowerCase();
  var level = { verde: 1, amarillo: 2, naranja: 3, rojo: 4 }[nivel] || 0;
  if (level <= 1) return null;
  var onset = isoMs(tagVal(xml, 'onset') || tagVal(xml, 'effective'));
  var expires = isoMs(tagVal(xml, 'expires'));
  if (expires && nowMs > expires) return null;
  if (onset && nowMs + 3600000 < onset) return null;   // still >1h away: not yet
  var fen = capParam(xml, 'fenomeno') || tagVal(xml, 'event');
  return { level: level, label: fenLabel(fen) };
}

// The whole archive -> the single highest active warning, or null.
function parseAvisoTar(bytes, nowMs) {
  var files = untar(bytes), best = null;
  for (var i = 0; i < files.length; i++) {
    if (!/\.xml$/i.test(files[i].name) && files[i].text.indexOf('<alert') < 0) continue;
    var a = capAlert(files[i].text, nowMs);
    if (a && (!best || a.level > best.level)) best = a;
  }
  return best;
}

module.exports = {
  untar: untar, capAlert: capAlert, parseAvisoTar: parseAvisoTar, ALERT: ALERT,
};
