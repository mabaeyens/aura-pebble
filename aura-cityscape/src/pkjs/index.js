// Aura Cityscape Weather: phone-side bridge.
// Resolves a location (picked city or typed coordinates, else GPS, else Madrid),
// fetches from Open-Meteo (or AEMET in Spain, when enabled with a key), normalises,
// and ships the forecast to the watch in framed AppMessages. Clay drives units,
// location and the AEMET fields; a Spanish location is resolved to its INE here to
// route it to AEMET.

var Clay = require('pebble-clay');
var clayConfig = require('./config');
var providers = require('./providers');
var geo = require('./geo');
var derive = require('./derive');

var clay = new Clay(clayConfig.config, clayConfig.customFn, { autoHandleEvents: false });

var HOURS_N = 8;
var DAYS_N = 6;
var REFRESH_MS = 30 * 60 * 1000;
var SEED = { name: 'Madrid', lat: 40.4168, lon: -3.7038 };   // fallback if nothing else resolves

// Units default follows the phone locale until the user sets the Clay toggle:
// metric everywhere except the handful of imperial-first locales.
function defaultMetric() {
  try {
    var lang = (navigator.language || 'en').toLowerCase();
    if (lang.indexOf('en-us') === 0 || lang.indexOf('en-lr') === 0 ||
        lang.indexOf('my') === 0) return false;
  } catch (e) {}
  return true;
}

function readSettings() {
  try { return JSON.parse(localStorage.getItem('clay-settings')) || {}; }
  catch (e) { return {}; }
}

function isMetric(s) {
  if (s.UNITS === 'f') return false;
  if (s.UNITS === 'c') return true;
  return defaultMetric();               // 'auto' or unset
}

// Location precedence (docs/02): the picked city / manual coordinates win when
// GPS is switched off (picking a city switches it off); otherwise GPS, then the
// seed. GPS defaults on so a fresh install works before any city is chosen.
function resolveLocation(s, cb) {
  var lat = parseFloat(String(s.LAT).replace(',', '.')), lon = parseFloat(String(s.LON).replace(',', '.'));
  var hasCoords = !isNaN(lat) && !isNaN(lon) && Math.abs(lat) <= 90 && Math.abs(lon) <= 180;
  var city = hasCoords ? { name: s.CITY || SEED.name, lat: lat, lon: lon } : null;
  var useGPS = s.USE_GPS === undefined ? true : !!s.USE_GPS;

  if (!useGPS && city) { cb(city); return; }

  if (useGPS) {
    navigator.geolocation.getCurrentPosition(
      function (pos) { cb({ name: 'My location', lat: pos.coords.latitude, lon: pos.coords.longitude }); },
      function () { cb(city || SEED); },
      { timeout: 12000, maximumAge: 600000 });
    return;
  }
  cb(city || SEED);
}

// Chain the forecast to the watch: one current-conditions frame, then the 8 hourly
// and 6 daily frames, each sent only after the previous one was acknowledged, so
// PebbleKit JS never drops overlapping sends. Only what the watch draws is sent.
var sending = 0;   // timestamp when a chain started, 0 when idle;   // a chain is in flight; a second refresh waits (a stuck chain is ignored after 60 s)

function sendWeather(w) {
  derive.enrich(w);   // moon phase and illumination, computed here (the watch has no almanac)
  var frames = [{
    WX_OK: 1, WX_NAME: w.name, WX_TEMP: w.temp, WX_TMIN: w.tmin, WX_TMAX: w.tmax,
    WX_CODE: w.code, WX_POP: w.pop,
    WX_SUNRISE: w.sunrise, WX_SUNSET: w.sunset, WX_UNITS: w.is_metric, WX_UPDATED: w.updated,
    WX_WIND: w.wind_speed, WX_MOON: w.moon_phase, WX_MOONILL: w.moon_illum,
  }];
  for (var i = 0; i < HOURS_N; i++) {
    frames.push({ H_IDX: i, H_TEMP: w.hours[i].temp, H_CODE: w.hours[i].code, H_POP: w.hours[i].pop });
  }
  for (var d = 0; d < DAYS_N; d++) {
    frames.push({ D_IDX: d, D_MIN: w.days[d].min, D_MAX: w.days[d].max,
                  D_CODE: w.days[d].code, D_POP: w.days[d].pop });
  }
  sending = Date.now();
  sendChain(frames, 0);
}

function sendChain(frames, i) {
  if (i === 0) console.log('Aura Cityscape: sending forecast for ' + frames[0].WX_NAME + ', ' + frames[0].WX_TEMP + ', code ' + frames[0].WX_CODE);
  if (i >= frames.length) { sending = 0; return; }
  Pebble.sendAppMessage(frames[i],
    function () { sendChain(frames, i + 1); },
    function () { sendChain(frames, i + 1); });   // skip a dropped frame; next refresh heals it
}

function openMeteo(loc, metric) {
  providers.fetchOpenMeteo(loc, metric, sendWeather, function (err) {
    console.log('Aura Cityscape: open-meteo failed: ' + err);
    Pebble.sendAppMessage({ WX_OK: 0 });            // keep the persisted forecast on screen
  });
}

// Route to AEMET only for a Spanish location when the user enabled it and gave a
// key; otherwise Open-Meteo. AEMET failure (429, bad key) falls back to
// Open-Meteo rather than blanking the screen.
function refresh() {
  if (sending && Date.now() - sending < 60000) return;
  var s = readSettings();
  var metric = isMetric(s);
  resolveLocation(s, function (loc) {
    console.log('Aura Cityscape: location ' + loc.name + ' ' + loc.lat + ',' + loc.lon);
    loc.ine = geo.nearestINE(loc.lat, loc.lon);
    if (loc.ine && s.USE_AEMET && s.AEMET_KEY) {
      loc.aemetKey = s.AEMET_KEY;
      loc.ccaa = geo.ccaaForINE(loc.ine);
      loc.avisoArea = geo.avisoAreaForINE(loc.ine);
      providers.fetchAEMET(loc, metric, sendWeather, function (err) {
        console.log('Aura Cityscape: aemet failed (' + err + '), using open-meteo');
        openMeteo(loc, metric);
      });
    } else {
      openMeteo(loc, metric);
    }
  });
}

Pebble.addEventListener('ready', function () {
  console.log('Aura Cityscape: ready');
  refresh();
  setInterval(refresh, REFRESH_MS);
});

// The watch's SELECT button sends REFRESH; refetch on demand.
Pebble.addEventListener('appmessage', function (e) {
  if (e.payload && e.payload.REFRESH !== undefined) refresh();
});

// Clay config lifecycle (we handle it so we can refresh right after a save).
Pebble.addEventListener('showConfiguration', function () {
  Pebble.openURL(clay.generateUrl());
});
Pebble.addEventListener('webviewclosed', function (e) {
  if (!e || !e.response) return;
  clay.getSettings(e.response);    // persists to localStorage under 'clay-settings'
  refresh();
});
