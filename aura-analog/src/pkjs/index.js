// Aura Analog — PebbleKit JS: Clay settings + Open-Meteo weather bridge.
// The watch has no internet; this runs on the phone, fetches weather, and pushes
// temperature + WMO weather code to the watch over AppMessage. Open-Meteo needs
// no API key, which keeps this free for anyone installing the face.

var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

function settings() {
  try { return JSON.parse(localStorage.getItem('clay-settings')) || {}; }
  catch (e) { return {}; }
}

function sendError() {
  Pebble.sendAppMessage({ 'WX_OK': 0 });
}

function fetchWeather(lat, lon) {
  var s = settings();
  var unit = s.UNITS ? '&temperature_unit=fahrenheit' : '';
  var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat +
            '&longitude=' + lon + '&current=temperature_2m,weather_code' + unit;
  var xhr = new XMLHttpRequest();
  xhr.timeout = 15000;
  xhr.onload = function () {
    try {
      var d = JSON.parse(xhr.responseText);
      Pebble.sendAppMessage({
        'WX_TEMP': Math.round(d.current.temperature_2m),
        'WX_CODE': d.current.weather_code | 0,
        'WX_OK': 1
      });
    } catch (e) { sendError(); }
  };
  xhr.onerror = function () { sendError(); };
  xhr.ontimeout = function () { sendError(); };
  xhr.open('GET', url);
  xhr.send();
}

function locateAndFetch() {
  var s = settings();
  var lat = parseFloat(s.LAT), lon = parseFloat(s.LON);
  if (s.LOCMODE && !isNaN(lat) && !isNaN(lon)) {
    fetchWeather(lat, lon);
    return;
  }
  navigator.geolocation.getCurrentPosition(
    function (pos) { fetchWeather(pos.coords.latitude, pos.coords.longitude); },
    function () { sendError(); },
    { timeout: 15000, maximumAge: 600000 }
  );
}

Pebble.addEventListener('ready', function () {
  locateAndFetch();
  setInterval(locateAndFetch, 30 * 60 * 1000); // refresh every 30 minutes
});

// The watch (or a settings save) can trigger an immediate refresh by messaging us.
Pebble.addEventListener('appmessage', function () {
  locateAndFetch();
});
