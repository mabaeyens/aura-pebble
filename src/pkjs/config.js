// Clay configuration for Aura Weather (docs/02). Standard Clay components carry
// units, GPS and the AEMET fields; the one custom piece is the worldwide city
// typeahead, which a declarative config cannot do, so `customFn` (injected into
// the config page and therefore self-contained: no require, no closures) wires a
// debounced search against the keyless Open-Meteo geocoder and fills the
// location fields on tap. A Spanish pick is resolved to its INE later, on the
// phone (geo.js), so the 737 KB municipality table never enters this page.

var config = [
  {
    "type": "heading",
    "defaultValue": "Aura Weather",
  },
  {
    "type": "text",
    "defaultValue": "Live weather, worldwide, no account needed. Search a city or use GPS.",
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Location" },
      { "type": "input", "messageKey": "SEARCH", "label": "Search a city",
        "attributes": { "placeholder": "e.g. Tokyo, Teruel", "autocomplete": "off" } },
      { "type": "input", "messageKey": "CITY", "label": "Selected",
        "attributes": { "placeholder": "none yet" } },
      { "type": "toggle", "messageKey": "USE_GPS",
        "label": "Use phone GPS (off = the city above)", "defaultValue": true },
      { "type": "text",
        "defaultValue": "Latitude / longitude fill in when you pick a city; edit them to set a location by hand." },
      { "type": "input", "messageKey": "LAT", "label": "Latitude",
        "attributes": { "placeholder": "40.4168", "type": "text" } },
      { "type": "input", "messageKey": "LON", "label": "Longitude",
        "attributes": { "placeholder": "-3.7038", "type": "text" } },
    ],
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Units" },
      { "type": "select", "messageKey": "UNITS", "label": "Temperature", "defaultValue": "auto",
        "options": [
          { "label": "Automatic (phone locale)", "value": "auto" },
          { "label": "Celsius", "value": "c" },
          { "label": "Fahrenheit", "value": "f" },
        ] },
    ],
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "AEMET (Spain)" },
      { "type": "text",
        "defaultValue": "Optional. In Spain, use the official AEMET source instead of Open-Meteo. Needs a free API key from opendata.aemet.es." },
      { "type": "toggle", "messageKey": "USE_AEMET",
        "label": "Use AEMET in Spain", "defaultValue": false },
      { "type": "input", "messageKey": "AEMET_KEY", "label": "AEMET API key",
        "attributes": { "placeholder": "paste your key", "autocomplete": "off" } },
    ],
  },
  { "type": "submit", "defaultValue": "Save" },
];

// Injected into the config page. `this` is the ClayConfig; `minified` is Clay's
// bundled DOM helper. Kept dependency-free because Clay serialises it to a string.
function customFn(minified) {
  var clayConfig = this;

  clayConfig.on(clayConfig.EVENTS.AFTER_RENDER, function () {
    var searchItem = clayConfig.getItemByMessageKey('SEARCH');
    if (!searchItem || !searchItem.$element) return;

    var wrapper = searchItem.$element.get(0);
    var box = document.createElement('div');
    box.style.margin = '0 0 6px 0';
    box.style.background = '#1b1b1b';
    wrapper.appendChild(box);

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
          row.style.padding = '10px 12px';
          row.style.borderBottom = '1px solid #333';
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
            if (x.admin1) parts.push(x.admin1);
            if (x.country) parts.push(x.country);
            rows.push({ name: x.name, lat: x.latitude, lon: x.longitude, label: parts.join(', ') });
          }
        } catch (e) {}
        render(rows);
      };
      req.onerror = function () { render([]); };
      req.send();
    }

    // Debounce keystrokes ~300 ms; the current value comes from the Clay item.
    searchItem.$manipulatorTarget.on('keyup', function () {
      if (timer) clearTimeout(timer);
      var q = String(searchItem.get() || '').trim();
      if (q.length < 2) { box.innerHTML = ''; return; }
      timer = setTimeout(function () { search(q); }, 300);
    });
  });
}

module.exports = { config: config, customFn: customFn };
