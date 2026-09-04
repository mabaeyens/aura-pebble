// Provider fetchers. Each returns the identical normalised object via callback,
// so index.js never branches on provider past the router. Step 2 ships the
// keyless worldwide provider (Open-Meteo); fetchAEMET arrives in step 5.
// See docs/01-data-bridge.md.

var N = require('./normalise');
var cap = require('./capaviso');

var HOURS_N = 8;
var DAYS_N = 6;

function xhrJSON(url, timeoutMs, onOk, onErr) {
  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.timeout = timeoutMs || 15000;
  req.onload = function () {
    if (req.status >= 200 && req.status < 300) {
      try { onOk(JSON.parse(req.responseText)); }
      catch (e) { onErr('parse: ' + e); }
    } else {
      onErr('http ' + req.status);
    }
  };
  req.onerror = function () { onErr('network'); };
  req.ontimeout = function () { onErr('timeout'); };
  req.send();
}

// Same as xhrJSON but hands back the raw response text (the AEMET boletín datos
// is a plain-text bulletin, not JSON).
function xhrText(url, timeoutMs, onOk, onErr) {
  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.timeout = timeoutMs || 15000;
  req.onload = function () {
    if (req.status >= 200 && req.status < 300) onOk(req.responseText);
    else onErr('http ' + req.status);
  };
  req.onerror = function () { onErr('network'); };
  req.ontimeout = function () { onErr('timeout'); };
  req.send();
}

// One Open-Meteo call gives current, hourly, daily and sun times. timeformat=
// unixtime means sunrise/sunset and hourly.time come back as unix seconds, so no
// fragile ISO-string date parsing in the sandbox. temperature_unit handles F/C
// on the phone; the watch stays unit-agnostic.
function fetchOpenMeteo(loc, isMetric, onOk, onErr) {
  var url = 'https://api.open-meteo.com/v1/forecast'
    + '?latitude=' + loc.lat + '&longitude=' + loc.lon
    + '&current=temperature_2m,relative_humidity_2m,weather_code,apparent_temperature,'
    +   'wind_speed_10m,wind_direction_10m,wind_gusts_10m,precipitation'
    + '&hourly=temperature_2m,weather_code,precipitation_probability,uv_index'
    + '&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,uv_index_max,sunrise,sunset'
    + '&forecast_days=' + DAYS_N + '&timezone=auto&timeformat=unixtime'
    + (isMetric ? '' : '&temperature_unit=fahrenheit&wind_speed_unit=mph');

  xhrJSON(url, 15000, function (j) {
    try { onOk(shape(j, loc, isMetric)); }
    catch (e) { onErr('shape: ' + e); }
  }, onErr);
}

// Turn the Open-Meteo payload into the one normalised object the watch renders.
function shape(j, loc, isMetric) {
  var cur = j.current || {};
  var h = j.hourly || {};
  var d = j.daily || {};
  var now = Math.floor(Date.now() / 1000);

  // Anchor the hourly strip to the first slot at or after the current hour.
  var start = 0;
  if (h.time && h.time.length) {
    for (var i = 0; i < h.time.length; i++) {
      if (h.time[i] >= now - 1800) { start = i; break; }
    }
  }

  var hours = [];
  for (var k = 0; k < HOURS_N; k++) {
    var idx = start + k;
    if (h.time && idx < h.time.length) {
      hours.push({
        temp: N.i8(h.temperature_2m[idx]),
        code: N.wmoToCode(h.weather_code[idx]),
        pop: N.u8(h.precipitation_probability ? h.precipitation_probability[idx] : 0),
      });
    } else {
      hours.push({ temp: 0, code: 1, pop: 0 });
    }
  }

  var days = [];
  for (var m = 0; m < DAYS_N; m++) {
    if (d.time && m < d.time.length) {
      days.push({
        min: N.i8(d.temperature_2m_min[m]),
        max: N.i8(d.temperature_2m_max[m]),
        code: N.wmoToCode(d.weather_code[m]),
        pop: N.u8(d.precipitation_probability_max ? d.precipitation_probability_max[m] : 0),
      });
    } else {
      days.push({ min: 0, max: 0, code: 1, pop: 0 });
    }
  }

  return {
    ok: 1,
    name: loc.name || 'My location',
    temp: N.i8(cur.temperature_2m),
    tmin: days[0].min,
    tmax: days[0].max,
    code: N.wmoToCode(cur.weather_code),
    humidity: N.u8(cur.relative_humidity_2m),
    pop: hours[0].pop,                                  // current pop = next hour's
    feels_like: N.i8(cur.apparent_temperature),
    wind_speed: N.u8(cur.wind_speed_10m),
    wind_dir: N.dir16(cur.wind_direction_10m),
    wind_gust: N.u8(cur.wind_gusts_10m),
    precip_mm: N.u8(cur.precipitation),
    storm_prob: 0,                                      // no free thunderstorm-prob field
    uv: N.u8((h.uv_index && h.uv_index[start]) || 0),
    uv_peak: N.u8((d.uv_index_max && d.uv_index_max[0]) || 0),
    aqi: 0,                                             // MITECO ICA is Spain-only, Phase B
    sunrise: (d.sunrise && d.sunrise[0]) || 0,
    sunset: (d.sunset && d.sunset[0]) || 0,
    is_metric: isMetric ? 1 : 0,
    hours: hours,
    days: days,
    updated: now,
  };
}

// ---- air quality (worldwide, keyless) --------------------------------------
// Open-Meteo's free Air Quality API gives a European AQI anywhere, the same
// worldwide-floor bet as the weather itself. It is a separate host and call, so
// it enriches the forecast after the main fetch and never blocks it: any failure
// calls back 0 (no card). Spain's official MITECO station ICA can override this
// later, exactly as AEMET overrides the weather.
function fetchAir(loc, cb) {
  var url = 'https://air-quality-api.open-meteo.com/v1/air-quality'
    + '?latitude=' + loc.lat + '&longitude=' + loc.lon
    + '&current=european_aqi&timezone=auto';
  xhrJSON(url, 12000, function (j) {
    cb(N.aqiBand(j && j.current ? j.current.european_aqi : null));
  }, function () { cb(0); });
}

// ---- AEMET (Spain, key required) -------------------------------------------
// Two-call model per endpoint: an envelope carrying a temporary `datos` URL,
// then the payload at that URL (docs/01). Daily and hourly are separate
// endpoints. AEMET always reports Celsius, so F is converted here; the hourly
// product carries orto/ocaso, so no sun-time maths is needed. On any failure the
// caller falls back to Open-Meteo, so this never blanks the screen.

var AEMET_BASE = 'https://opendata.aemet.es/opendata/api/prediccion/especifica/municipio/';

function aemetEnvelope(url, onOk, onErr) {
  xhrJSON(url, 15000, function (env) {
    if (env.estado === 429) { onErr('aemet 429'); return; }
    if (!env.datos) { onErr('aemet estado ' + env.estado); return; }
    xhrJSON(env.datos, 15000, onOk, onErr);   // the signed datos URL needs no key
  }, onErr);
}

function fetchAEMET(loc, isMetric, onOk, onErr) {
  var key = encodeURIComponent(loc.aemetKey);
  aemetEnvelope(AEMET_BASE + 'diaria/' + loc.ine + '?api_key=' + key, function (daily) {
    aemetEnvelope(AEMET_BASE + 'horaria/' + loc.ine + '?api_key=' + key, function (hourly) {
      try { onOk(shapeAEMET(daily, hourly, loc, isMetric)); }
      catch (e) { onErr('aemet shape: ' + e); }
    }, onErr);
  }, onErr);
}

function toF(c, isMetric) { return isMetric ? c : Math.round(c * 9 / 5 + 32); }
function pInt(s) { var n = parseInt(s, 10); return isNaN(n) ? 0 : n; }
// AEMET reports wind in km/h; imperial users see mph.
function windKmh(kmh, isMetric) { return isMetric ? kmh : Math.round(kmh * 0.621); }

// Wind at hour `hh` from AEMET horaria, tolerating both the older `viento`
// [{direccion, velocidad, periodo}] and the newer `vientoAndRachaMax`
// [{direccion:[], velocidad:[], value (gust), periodo}] shapes.
function windAt(day, hh) {
  var arr = day.vientoAndRachaMax || day.viento || [];
  for (var i = 0; i < arr.length; i++) {
    if (arr[i].periodo !== hh) continue;
    var v = arr[i];
    var spd = Array.isArray(v.velocidad) ? v.velocidad[0] : v.velocidad;
    var dir = Array.isArray(v.direccion) ? v.direccion[0] : v.direccion;
    return { spd: pInt(spd), dir: dir, gust: pInt(v.value) };
  }
  return { spd: 0, dir: '', gust: 0 };
}

// Index an AEMET hourly array ({value, periodo}) by its two-digit hour periodo.
function byHour(arr) {
  var m = {};
  for (var i = 0; arr && i < arr.length; i++) m[arr[i].periodo] = arr[i].value;
  return m;
}

// Precip probability per hour from AEMET's coarse blocks (periodo like "0814").
function popByHour(arr) {
  var out = [];
  for (var h = 0; h < 24; h++) out.push(0);
  for (var i = 0; arr && i < arr.length; i++) {
    var p = arr[i].periodo || '';
    if (p.length !== 4) continue;
    var a = pInt(p.slice(0, 2)), b = pInt(p.slice(2, 4)), v = pInt(arr[i].value);
    for (var h2 = a; h2 < b && h2 < 24; h2++) out[h2] = v;
  }
  return out;
}

// The daily sky is the daytime block; daily precip is the max across blocks.
function daySky(blocks) {
  var pref = ['12-24', '00-24', '12-18', '06-12'];
  for (var k = 0; k < pref.length; k++) {
    for (var i = 0; blocks && i < blocks.length; i++) {
      if (blocks[i].periodo === pref[k] && blocks[i].value) return blocks[i].value;
    }
  }
  for (var j = 0; blocks && j < blocks.length; j++) if (blocks[j].value) return blocks[j].value;
  return '';
}
function dayMaxProb(blocks) {
  var mx = 0;
  for (var i = 0; blocks && i < blocks.length; i++) {
    var v = pInt(blocks[i].value);
    if (v > mx) mx = v;
  }
  return mx;
}

function hhmmToday(hhmm) {
  var p = String(hhmm || '').split(':');
  var d = new Date();
  d.setHours(pInt(p[0]), pInt(p[1]), 0, 0);
  return Math.floor(d.getTime() / 1000);
}

function shapeAEMET(daily, hourly, loc, isMetric) {
  var dd = daily[0].prediccion.dia;      // daily blocks per day
  var hd = hourly[0].prediccion.dia;     // hourly arrays per day
  var now = new Date();
  var nowHour = now.getHours();
  var unixNow = Math.floor(Date.now() / 1000);

  // Next 8 hours across today (from the current hour) and tomorrow.
  var hours = [];
  for (var di = 0; di < hd.length && hours.length < HOURS_N; di++) {
    var day = hd[di];
    var temp = byHour(day.temperatura);
    var sky = byHour(day.estadoCielo);
    var pops = popByHour(day.probPrecipitacion);
    for (var h = 0; h < 24 && hours.length < HOURS_N; h++) {
      var pp = ('0' + h).slice(-2);
      if (temp[pp] === undefined) continue;
      if (di === 0 && h < nowHour) continue;
      hours.push({ temp: N.i8(toF(pInt(temp[pp]), isMetric)),
                   code: N.aemetToCode(sky[pp] || ''), pop: N.u8(pops[h]) });
    }
  }
  while (hours.length < HOURS_N) hours.push({ temp: 0, code: 1, pop: 0 });

  // Six days from today.
  var days = [];
  for (var m = 0; m < DAYS_N; m++) {
    if (m < dd.length) {
      var da = dd[m];
      var t = da.temperatura || {};
      days.push({ min: N.i8(toF(pInt(t.minima), isMetric)),
                  max: N.i8(toF(pInt(t.maxima), isMetric)),
                  code: N.aemetToCode(daySky(da.estadoCielo)),
                  pop: N.u8(dayMaxProb(da.probPrecipitacion)) });
    } else {
      days.push({ min: 0, max: 0, code: 1, pop: 0 });
    }
  }

  var hh = ('0' + nowHour).slice(-2);
  var hum = byHour(hd[0].humedadRelativa);
  var feels = byHour(hd[0].sensTermica);          // apparent temp, may be absent
  var precip = byHour(hd[0].precipitacion);
  var wind = windAt(hd[0], hh);

  return {
    ok: 1,
    name: loc.name || (daily[0].nombre) || 'Spain',
    temp: hours[0].temp,
    tmin: days[0].min,
    tmax: days[0].max,
    code: hours[0].code,
    humidity: N.u8(pInt(hum[hh])),
    pop: hours[0].pop,
    feels_like: feels[hh] !== undefined ? N.i8(toF(pInt(feels[hh]), isMetric)) : hours[0].temp,
    wind_speed: N.u8(windKmh(wind.spd, isMetric)),
    wind_dir: N.dirAemet(wind.dir),
    wind_gust: N.u8(windKmh(wind.gust, isMetric)),
    precip_mm: N.u8(pInt(precip[hh])),
    storm_prob: 0,
    uv: N.u8(pInt(dd[0].uvMax)),
    uv_peak: N.u8(pInt(dd[0].uvMax)),
    aqi: 0,
    sunrise: hhmmToday(hd[0].orto),
    sunset: hhmmToday(hd[0].ocaso),
    is_metric: isMetric ? 1 : 0,
    hours: hours,
    days: days,
    updated: unixNow,
  };
}

// ---- AEMET official boletín (Spain, key required) --------------------------
// The regional (autonomous-community) and national forecast bulletins are plain
// text, so unlike the CAP aviso they parse cleanly in the sandbox. The regional
// boletín replaces Aura's generated bulletin in Spain (docs/06 Phase C); the
// national one is the fallback when the region has no code or no text. On any
// failure the caller keeps the generated bulletin, so this never blanks the card.

var AEMET_CCAA = 'https://opendata.aemet.es/opendata/api/prediccion/ccaa/hoy/';
var AEMET_NAC  = 'https://opendata.aemet.es/opendata/api/prediccion/nacional/hoy';

function aemetEnvelopeText(url, onOk, onErr) {
  xhrJSON(url, 15000, function (env) {
    if (env.estado === 429) { onErr('aemet 429'); return; }
    if (!env.datos) { onErr('aemet estado ' + env.estado); return; }
    xhrText(env.datos, 15000, onOk, onErr);
  }, onErr);
}

// Condense a full AEMET bulletin into the ~220 chars the card holds: prefer the
// PREDICCIÓN body, drop the "A.- ..." section markers, collapse whitespace, and
// strip any character the bundled font subset cannot draw so nothing renders as
// tofu (the font carries Spanish accents plus a little punctuation).
function condenseBoletin(text) {
  if (!text) return '';
  var t = String(text);
  // The forecast body is the "B.- PREDICCIÓN" section; the leading "PREDICCIÓN
  // PARA LA COMUNIDAD ..." is only the title, so match the marker-prefixed form.
  var m = t.match(/[A-Z]\.-\s*PREDICCI[ÓO]N[^\n]*\n([\s\S]*)/i);
  var body = m ? m[1] : t;
  body = body.replace(/^\s*[A-Z]\.-.*$/gm, ' ')            // "A.- ..." section markers
             .replace(/^[^a-z\n]{6,}$/gm, ' ')             // ALL-CAPS header/date lines
             .replace(/[^0-9A-Za-zàáâäãèéêëìíîïòóôöõùúûüñçøåÀÁÂÄÃÈÉÊËÌÍÎÏÒÓÔÖÕÙÚÛÜÑÇØÅ°ºª%\/,.:();!' -]/g, ' ')
             .replace(/\s+/g, ' ')
             .trim();
  if (body.length > 220) body = body.slice(0, 217) + '...';
  return body;
}

function fetchBoletin(loc, cb) {
  var key = encodeURIComponent(loc.aemetKey);
  function national() {
    aemetEnvelopeText(AEMET_NAC + '?api_key=' + key,
      function (txt) { cb(condenseBoletin(txt)); },
      function () { cb(''); });
  }
  if (!loc.ccaa) { national(); return; }
  aemetEnvelopeText(AEMET_CCAA + loc.ccaa + '?api_key=' + key,
    function (txt) { var c = condenseBoletin(txt); if (c) cb(c); else national(); },
    function () { national(); });
}

// ---- AEMET official CAP aviso (Spain, key required) ------------------------
// The avisos_cap endpoint returns an envelope whose datos is a tar archive of
// CAP XML warnings. capaviso.js does the untar + parse; here is the transport.
// Everything short-circuits to cb(null) on failure, so the derived aviso stands:
// a bad area code, an HTTP error, a gzip payload the sandbox cannot inflate, or
// a parse slip all leave the worldwide threshold advisory in place.

var AEMET_AVISO = 'https://opendata.aemet.es/opendata/api/avisos_cap/ultimoelaborado/area/';

function xhrArrayBuffer(url, timeoutMs, onOk, onErr) {
  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.responseType = 'arraybuffer';
  req.timeout = timeoutMs || 15000;
  req.onload = function () {
    if (req.status >= 200 && req.status < 300 && req.response) onOk(req.response);
    else onErr('http ' + req.status);
  };
  req.onerror = function () { onErr('network'); };
  req.ontimeout = function () { onErr('timeout'); };
  req.send();
}

function fetchAviso(loc, cb) {
  if (!loc.avisoArea) { cb(null); return; }
  var key = encodeURIComponent(loc.aemetKey);
  xhrJSON(AEMET_AVISO + loc.avisoArea + '?api_key=' + key, 15000, function (env) {
    if (env.estado === 429 || !env.datos) { cb(null); return; }
    xhrArrayBuffer(env.datos, 15000, function (buf) {
      try {
        var bytes = new Uint8Array(buf);
        if (bytes.length < 2 || (bytes[0] === 0x1f && bytes[1] === 0x8b)) { cb(null); return; }
        cb(cap.parseAvisoTar(bytes, Date.now()));   // null when no active warning
      } catch (e) { cb(null); }
    }, function () { cb(null); });
  }, function () { cb(null); });
}

module.exports = {
  fetchOpenMeteo: fetchOpenMeteo, fetchAEMET: fetchAEMET, fetchAir: fetchAir,
  fetchBoletin: fetchBoletin, condenseBoletin: condenseBoletin, fetchAviso: fetchAviso,
};
