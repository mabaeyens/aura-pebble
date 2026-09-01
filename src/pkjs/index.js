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
