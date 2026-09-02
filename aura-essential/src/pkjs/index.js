// Aura Essential PebbleKit JS: Clay settings + Open-Meteo weather bridge.
// The watch has no internet; this runs on the phone, fetches weather, and pushes
// temperature + WMO weather code to the watch over AppMessage. Open-Meteo needs
// no API key, which keeps this free for anyone installing the face.

var Clay = require('pebble-clay');
var clayConfig = require('./config');

// Runs on the generated config page (injected via toString, so it must be fully
// self-contained). Gives the page an Aura identity instead of the plain default:
// a branded header, wide rounded section cards, aligned labels, and an accent
// Save button in Aura's sky colours.
function clayStyle() {
  var css = [
    'body{background:#0e1116;color:#e6edf3;}',
    '#aura-hd{padding:26px 16px 20px;text-align:center;',
      'background:linear-gradient(160deg,#20344f,#0e1116);}',
    '#aura-hd .wm{font-size:30px;font-weight:700;letter-spacing:9px;color:#fff;}',
    '#aura-hd .sub{margin-top:6px;font-size:12px;letter-spacing:2px;',
      'color:#8aa0b6;text-transform:uppercase;}',
    '.section{margin:16px 12px;border-radius:16px;overflow:hidden;',
      'background:#171d27;box-shadow:0 1px 6px rgba(0,0,0,.45);}',
    '.item{padding:15px 18px;}',
    '.label{font-size:16px;letter-spacing:.2px;color:#e6edf3;}',
    '.description{color:#8aa0b6;}',
    '.input{border-radius:10px;padding:12px;}',
    '.component-submit .button,.button{margin:18px 12px;border:0;border-radius:14px;',
      'padding:15px;font-size:17px;letter-spacing:2px;text-transform:uppercase;color:#04222b;',
      'background:linear-gradient(90deg,#2ec4b6,#1f8fb0);}'
  ].join('');
  var st = document.createElement('style');
  st.innerHTML = css;
  document.head.appendChild(st);

  this.on(this.EVENTS.AFTER_BUILD, function () {
    if (document.getElementById('aura-hd')) { return; }
    var hd = document.createElement('div');
    hd.id = 'aura-hd';
    hd.innerHTML = '<div class="wm">AURA</div><div class="sub">Essential watchface</div>';
    document.body.insertBefore(hd, document.body.firstChild);
  });
}

var clay = new Clay(clayConfig, clayStyle);

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

// Coordinates for the city picker. Order MUST match CITY_OPTIONS in config.js
// (index = the option's value).
var CITIES = [
  { lat: 40.4168, lon: -3.7038 },   // 0  Madrid
  { lat: 41.3874, lon: 2.1686 },    // 1  Barcelona
  { lat: 39.4699, lon: -0.3763 },   // 2  Valencia
  { lat: 37.3891, lon: -5.9845 },   // 3  Sevilla
  { lat: 41.6488, lon: -0.8891 },   // 4  Zaragoza
  { lat: 36.7213, lon: -4.4214 },   // 5  Malaga
  { lat: 37.9922, lon: -1.1307 },   // 6  Murcia
  { lat: 39.5696, lon: 2.6502 },    // 7  Palma
  { lat: 28.1235, lon: -15.4363 },  // 8  Las Palmas
  { lat: 43.2630, lon: -2.9350 },   // 9  Bilbao
  { lat: 38.3452, lon: -0.4810 },   // 10 Alicante
  { lat: 37.8882, lon: -4.7794 },   // 11 Cordoba
  { lat: 41.6523, lon: -4.7245 },   // 12 Valladolid
  { lat: 42.2406, lon: -8.7207 },   // 13 Vigo
  { lat: 37.1773, lon: -3.5986 },   // 14 Granada
  { lat: 43.3623, lon: -8.4115 },   // 15 A Coruna
  { lat: 43.4623, lon: -3.8099 },   // 16 Santander
  { lat: 43.3183, lon: -1.9812 },   // 17 San Sebastian
  { lat: 28.4636, lon: -16.2518 },  // 18 Santa Cruz de Tenerife
  { lat: 42.8125, lon: -1.6458 },   // 19 Pamplona
  { lat: 51.5074, lon: -0.1278 },   // 20 London
  { lat: 48.8566, lon: 2.3522 },    // 21 Paris
  { lat: 38.7223, lon: -9.1393 }    // 22 Lisbon
];

function locateAndFetch() {
  var s = settings();
  var lat = parseFloat(s.LAT), lon = parseFloat(s.LON);
  if (s.LOCMODE && !isNaN(lat) && !isNaN(lon)) {   // manual coordinates win
    fetchWeather(lat, lon);
    return;
  }
  var ci = parseInt(s.CITY, 10);                   // then a picked city
  if (!isNaN(ci) && ci >= 0 && ci < CITIES.length) {
    fetchWeather(CITIES[ci].lat, CITIES[ci].lon);
    return;
  }
  navigator.geolocation.getCurrentPosition(        // else the phone's GPS
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
