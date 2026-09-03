// Aura Weather - phone-side bridge (docs/01-data-bridge.md).
// Step 2: the keyless Open-Meteo path end to end. Resolve a location (GPS, else
// a saved/seed fallback), fetch, normalise, and ship the forecast to the watch
// in framed AppMessages. Clay settings, worldwide search and the AEMET path land
// in later steps; this file is written so those only add, never rewrite.

var providers = require('./providers');

var HOURS_N = 8;
var DAYS_N = 6;
var REFRESH_MS = 30 * 60 * 1000;
var SEED = { name: 'Madrid', lat: 40.4168, lon: -3.7038 };   // fallback if GPS denied

// Units default follows the phone locale until the user sets the Clay toggle
// (step 4): metric everywhere except the handful of imperial-first locales.
function defaultMetric() {
  try {
    var lang = (navigator.language || 'en').toLowerCase();
    if (lang.indexOf('en-us') === 0 || lang.indexOf('en-lr') === 0 ||
        lang.indexOf('my') === 0) return false;
  } catch (e) {}
  return true;
}

function readSettings() {
  var s = {};
  try { s = JSON.parse(localStorage.getItem('clay-settings')) || {}; } catch (e) { s = {}; }
  return s;
}

function isMetric(s) {
  if (s && (s.UNITS === 'f' || s.UNITS === 'F' || s.UNITS === 0)) return false;
  if (s && (s.UNITS === 'c' || s.UNITS === 'C' || s.UNITS === 1)) return true;
  return defaultMetric();
}

// Where to fetch: GPS if the settings ask for it (or nothing is saved yet),
// otherwise the manually entered / last-picked location, otherwise the seed.
function resolveLocation(s, cb) {
  var manual = null;
  if (s && s.LAT && s.LON) {
    manual = { name: s.CITY || SEED.name, lat: parseFloat(s.LAT), lon: parseFloat(s.LON) };
  }
  var wantGPS = !s || !s.LOCMODE || s.LOCMODE === 'gps' || !manual;
  if (!wantGPS && manual) { cb(manual); return; }

  navigator.geolocation.getCurrentPosition(
    function (pos) {
      cb({ name: (manual && manual.name) || 'My location',
           lat: pos.coords.latitude, lon: pos.coords.longitude });
    },
    function () { cb(manual || SEED); },
    { timeout: 12000, maximumAge: 600000 }
  );
}

// Chain the forecast to the watch: one current message, then the 8 hourly and 6
// daily frames, each sent only after the previous one's outbox_sent fires, so
// PebbleKit JS never drops overlapping sends.
function sendWeather(w) {
  var frames = [];
  frames.push({
    WX_OK: 1, WX_NAME: w.name, WX_TEMP: w.temp, WX_TMIN: w.tmin, WX_TMAX: w.tmax,
    WX_CODE: w.code, WX_HUM: w.humidity, WX_POP: w.pop,
    WX_SUNRISE: w.sunrise, WX_SUNSET: w.sunset, WX_UNITS: w.is_metric, WX_UPDATED: w.updated,
  });
  for (var i = 0; i < HOURS_N; i++) {
    frames.push({ H_IDX: i, H_TEMP: w.hours[i].temp, H_CODE: w.hours[i].code, H_POP: w.hours[i].pop });
  }
  for (var d = 0; d < DAYS_N; d++) {
    frames.push({ D_IDX: d, D_MIN: w.days[d].min, D_MAX: w.days[d].max,
                  D_CODE: w.days[d].code, D_POP: w.days[d].pop });
  }
  sendChain(frames, 0);
}

function sendChain(frames, i) {
  if (i >= frames.length) return;
  Pebble.sendAppMessage(frames[i],
    function () { sendChain(frames, i + 1); },
    function () { sendChain(frames, i + 1); }   // skip a dropped frame; next refresh heals it
  );
}

function refresh() {
  var s = readSettings();
  var metric = isMetric(s);
  resolveLocation(s, function (loc) {
    providers.fetchOpenMeteo(loc, metric, function (w) {
      sendWeather(w);
    }, function (err) {
      console.log('Aura Weather: fetch failed: ' + err);
      Pebble.sendAppMessage({ WX_OK: 0 });          // keep the persisted forecast on screen
    });
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
