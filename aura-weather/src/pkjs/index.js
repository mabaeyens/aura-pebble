// Aura Weather - phone-side bridge (docs/01, docs/02).
// Resolves a location (manual/city coords, else GPS, else a seed), fetches from
// Open-Meteo, normalises, and ships the forecast to the watch in framed
// AppMessages. Clay drives units, location and the AEMET fields; a Spanish pick
// is resolved to its INE here so step 5 can route it to AEMET. Written so each
// later step only adds.

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
  var lat = parseFloat(s.LAT), lon = parseFloat(s.LON);
  var hasCoords = !isNaN(lat) && !isNaN(lon);
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

// Chain the forecast to the watch: one current message, then the 8 hourly and 6
// daily frames, each sent only after the previous outbox_sent, so PebbleKit JS
// never drops overlapping sends.
// The card-configuration frame from Clay: per-card visibility (default on) and
// the boot card (a stable card index; default 0 = hero). Sent first so the watch
// has the wearer's layout before it renders the forecast.
function cardConfig(s) {
  function on(k) { return s[k] === undefined ? 1 : (s[k] ? 1 : 0); }
  var boot = parseInt(s.CARD_BOOT, 10);
  return {
    CFG: 1,
    SHOW_HOURLY: on('SHOW_HOURLY'), SHOW_DAILY: on('SHOW_DAILY'),
    SHOW_SUNMOON: on('SHOW_SUNMOON'), SHOW_WIND: on('SHOW_WIND'),
    SHOW_UV: on('SHOW_UV'), SHOW_AIR: on('SHOW_AIR'),
    SHOW_DETAILS: on('SHOW_DETAILS'), SHOW_BULLETIN: on('SHOW_BULLETIN'),
    CARD_BOOT: isNaN(boot) ? 0 : boot,
    CARD_ORDER: s.CARD_ORDER || '2,3,4,5,6,7,8,9',
  };
}

function sendWeather(w) {
  derive.enrich(w);   // moon phase + the derived aviso (kept if AEMET set an official one)
  var frames = [cardConfig(readSettings()), {
    WX_OK: 1, WX_NAME: w.name, WX_TEMP: w.temp, WX_TMIN: w.tmin, WX_TMAX: w.tmax,
    WX_CODE: w.code, WX_HUM: w.humidity, WX_POP: w.pop,
    WX_SUNRISE: w.sunrise, WX_SUNSET: w.sunset, WX_UNITS: w.is_metric, WX_UPDATED: w.updated,
    WX_FEELS: w.feels_like, WX_WIND: w.wind_speed, WX_WDIR: w.wind_dir, WX_GUST: w.wind_gust,
    WX_PRECIP: w.precip_mm, WX_STORM: w.storm_prob,
    WX_UV: w.uv, WX_UVPEAK: w.uv_peak, WX_AQI: w.aqi,
    WX_MOON: w.moon_phase, WX_MOONILL: w.moon_illum,
    WX_ALEVEL: w.alert_level, WX_ALABEL: w.alert_label,
  }];
  for (var i = 0; i < HOURS_N; i++) {
    frames.push({ H_IDX: i, H_TEMP: w.hours[i].temp, H_CODE: w.hours[i].code, H_POP: w.hours[i].pop });
  }
  for (var d = 0; d < DAYS_N; d++) {
    frames.push({ D_IDX: d, D_MIN: w.days[d].min, D_MAX: w.days[d].max,
                  D_CODE: w.days[d].code, D_POP: w.days[d].pop });
  }
  // The bulletin rides its own frame, sent last (it is the largest payload).
  frames.push({ WX_BULL: w.bulletin || '' });
  sendChain(frames, 0);
}

function sendChain(frames, i) {
  if (i >= frames.length) return;
  Pebble.sendAppMessage(frames[i],
    function () { sendChain(frames, i + 1); },
    function () { sendChain(frames, i + 1); });   // skip a dropped frame; next refresh heals it
}

// Fetch the European AQI for this location, attach it (0 = no station/no data,
// leaves the air card hidden), then ship. Never blocks: fetchAir calls back 0 on
// any failure, so a missing air feed never delays or blanks the forecast.
function shipWith(loc) {
  return function (w) {
    providers.fetchAir(loc, function (aqi) {
      if (aqi > 0) w.aqi = aqi;
      sendWeather(w);
    });
  };
}

function openMeteo(loc, metric) {
  providers.fetchOpenMeteo(loc, metric, shipWith(loc), function (err) {
    console.log('Aura Weather: open-meteo failed: ' + err);
    Pebble.sendAppMessage({ WX_OK: 0 });            // keep the persisted forecast on screen
  });
}

// Route to AEMET only for a Spanish location when the user enabled it and gave a
// key; otherwise Open-Meteo. AEMET failure (429, bad key) falls back to
// Open-Meteo rather than blanking the screen.
function refresh() {
  var s = readSettings();
  var metric = isMetric(s);
  resolveLocation(s, function (loc) {
    loc.ine = geo.nearestINE(loc.lat, loc.lon);
    if (loc.ine && s.USE_AEMET && s.AEMET_KEY) {
      loc.aemetKey = s.AEMET_KEY;
      providers.fetchAEMET(loc, metric, shipWith(loc), function (err) {
        console.log('Aura Weather: aemet failed (' + err + '), using open-meteo');
        openMeteo(loc, metric);
      });
    } else {
      openMeteo(loc, metric);
    }
  });
}

Pebble.addEventListener('ready', function () {
  console.log('Aura Weather: ready');
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
