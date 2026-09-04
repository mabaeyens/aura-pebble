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
  var clayConfig = this;
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

  // Worldwide city typeahead under the "Search a city" box. A declarative Clay
  // config cannot do this, so it is wired here (this whole function is injected
  // into the config page as a string, so it must stay self-contained: no
  // require, no closures over module scope). Debounced query against the keyless
  // Open-Meteo geocoder; a tap fills CITY / LAT / LON and turns GPS off.
  clayConfig.on(clayConfig.EVENTS.AFTER_RENDER, function () {
    var searchItem = clayConfig.getItemByMessageKey('SEARCH');
    if (!searchItem || !searchItem.$element) { return; }

    var box = document.createElement('div');
    box.style.margin = '0 0 6px 0';
    box.style.background = '#171d27';
    searchItem.$element.get(0).appendChild(box);

    var timer = null;

    function setVal(key, value) {
      var item = clayConfig.getItemByMessageKey(key);
      if (item) { try { item.set(value); } catch (e) {} }
    }

    function pick(r) {
      setVal('CITY', r.name);
      setVal('LAT', String(r.lat));
      setVal('LON', String(r.lon));
      setVal('USE_GPS', false);   // picking a city means "use this city"
      box.innerHTML = '';
    }

    function render(rows) {
      box.innerHTML = '';
      for (var i = 0; i < rows.length; i++) {
        (function (r) {
          var row = document.createElement('div');
          row.textContent = r.label;
          row.style.padding = '12px 18px';
          row.style.borderBottom = '1px solid #2a3340';
          row.style.color = '#e6edf3';
          row.style.cursor = 'pointer';
          row.onclick = function () { pick(r); };
          box.appendChild(row);
        })(rows[i]);
      }
    }

    function search(q) {
      var url = 'https://geocoding-api.open-meteo.com/v1/search?name=' +
        encodeURIComponent(q) + '&count=8&language=en&format=json';
      var req = new XMLHttpRequest();
      req.open('GET', url, true);
      req.onload = function () {
        if (req.status < 200 || req.status >= 300) { render([]); return; }
        var rows = [];
        try {
          var j = JSON.parse(req.responseText);
          var res = j.results || [];
          for (var i = 0; i < res.length; i++) {
            var x = res[i];
            var parts = [x.name];
            if (x.admin1) { parts.push(x.admin1); }
            if (x.country) { parts.push(x.country); }
            rows.push({ name: x.name, lat: x.latitude, lon: x.longitude, label: parts.join(', ') });
          }
        } catch (e) {}
        render(rows);
      };
      req.onerror = function () { render([]); };
      req.send();
    }

    searchItem.$manipulatorTarget.on('keyup', function () {
      if (timer) { clearTimeout(timer); }
      var q = String(searchItem.get() || '').trim();
      if (q.length < 2) { box.innerHTML = ''; return; }
      timer = setTimeout(function () { search(q); }, 300);
    });
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
  // USE_GPS defaults on when unset. Off means use the searched city / manual
  // coordinates, which the typeahead fills into LAT/LON (CITY is display only).
  var useGps = (s.USE_GPS === undefined) ? true : s.USE_GPS;
  if (!useGps && !isNaN(lat) && !isNaN(lon)) {
    fetchWeather(lat, lon);
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
