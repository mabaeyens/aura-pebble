// Provider fetchers. Each returns the identical normalised object via callback,
// so index.js never branches on provider past the router. Step 2 ships the
// keyless worldwide provider (Open-Meteo); fetchAEMET arrives in step 5.
// See docs/01-data-bridge.md.

var N = require('./normalise');

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

// One Open-Meteo call gives current, hourly, daily and sun times. timeformat=
// unixtime means sunrise/sunset and hourly.time come back as unix seconds, so no
// fragile ISO-string date parsing in the sandbox. temperature_unit handles F/C
// on the phone; the watch stays unit-agnostic.
function fetchOpenMeteo(loc, isMetric, onOk, onErr) {
  var url = 'https://api.open-meteo.com/v1/forecast'
    + '?latitude=' + loc.lat + '&longitude=' + loc.lon
    + '&current=temperature_2m,relative_humidity_2m,weather_code'
    + '&hourly=temperature_2m,weather_code,precipitation_probability'
    + '&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max,sunrise,sunset'
    + '&forecast_days=' + DAYS_N + '&timezone=auto&timeformat=unixtime'
    + (isMetric ? '' : '&temperature_unit=fahrenheit');

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
    sunrise: (d.sunrise && d.sunrise[0]) || 0,
    sunset: (d.sunset && d.sunset[0]) || 0,
    is_metric: isMetric ? 1 : 0,
    hours: hours,
    days: days,
    updated: now,
  };
}

module.exports = { fetchOpenMeteo: fetchOpenMeteo };
