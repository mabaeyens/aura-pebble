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
      { "type": "heading", "defaultValue": "Appearance" },
      { "type": "select", "messageKey": "THEME", "label": "Theme", "defaultValue": "0",
        "description": "Also switches on the watch with a long press of the UP button.",
        "options": [
          { "label": "Dark (black background)", "value": "0" },
          { "label": "Light (white background)", "value": "1" },
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
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Cards" },
      { "type": "text",
        "defaultValue": "Choose which cards to keep and where the app opens. The hero is always shown; the aviso appears only during a warning." },
      { "type": "select", "messageKey": "CARD_BOOT", "label": "Open on", "defaultValue": "0",
        "options": [
          { "label": "Hero", "value": "0" },
          { "label": "Hourly", "value": "2" },
          { "label": "Daily", "value": "3" },
          { "label": "Sun & Moon", "value": "4" },
          { "label": "Wind", "value": "5" },
          { "label": "UV", "value": "6" },
          { "label": "Air quality", "value": "7" },
          { "label": "Details", "value": "8" },
          { "label": "Forecast", "value": "9" },
        ] },
      { "type": "text",
        "defaultValue": "Order: use the arrows to move a card up or down. The hero stays first and the aviso follows it." },
      { "type": "input", "messageKey": "CARD_ORDER", "label": "Card order",
        "defaultValue": "2,3,4,5,6,7,8,9" },
      { "type": "toggle", "messageKey": "SHOW_HOURLY", "label": "Hourly", "defaultValue": true },
      { "type": "toggle", "messageKey": "SHOW_DAILY", "label": "Daily", "defaultValue": true },
      { "type": "toggle", "messageKey": "SHOW_SUNMOON", "label": "Sun & Moon", "defaultValue": true },
      { "type": "toggle", "messageKey": "SHOW_WIND", "label": "Wind", "defaultValue": true },
      { "type": "toggle", "messageKey": "SHOW_UV", "label": "UV", "defaultValue": true },
      { "type": "toggle", "messageKey": "SHOW_AIR", "label": "Air quality", "defaultValue": true },
      { "type": "toggle", "messageKey": "SHOW_DETAILS", "label": "Details", "defaultValue": true },
      { "type": "toggle", "messageKey": "SHOW_BULLETIN", "label": "Forecast", "defaultValue": true },
    ],
  },
  { "type": "submit", "defaultValue": "Save" },
];

// Injected into the config page. `this` is the ClayConfig; `minified` is Clay's
// bundled DOM helper. Kept dependency-free because Clay serialises it to a string.
function customFn(minified) {
  var clayConfig = this;

  // Clay 1.0.4 only fires BEFORE_BUILD / AFTER_BUILD; there is no AFTER_RENDER,
  // and on(undefined, ...) throws and aborts the whole page (the bug that blanked
  // the essential settings screen). Wire on AFTER_BUILD instead.
  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function () {
    var searchItem = clayConfig.getItemByMessageKey('SEARCH');
    if (!searchItem || !searchItem.$element) return;

    // The config page runs Clay's `minified` DOM helper, where the raw node is
    // $element[0]; jQuery's .get(0) reads a property that is not there.
    var wrapper = searchItem.$element[0];
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

  // Card reorder widget. The wearer orders the eight body cards with up/down
  // arrows; the value is a CSV of card ids that the phone ships in the CFG frame
  // and the watch pages in. Standard Clay has no drag list, so this replaces the
  // raw CARD_ORDER text field with an arrow list (drag on a phone webview is
  // flaky; arrows are just as complete and testable). Hero and aviso are fixed
  // first, so only ids 2..9 appear here. Wired on AFTER_BUILD for the same reason
  // as the typeahead (Clay 1.0.4 has no AFTER_RENDER).
  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, function () {
    var orderItem = clayConfig.getItemByMessageKey('CARD_ORDER');
    if (!orderItem || !orderItem.$element) return;

    // Hide the raw CSV input; the arrow list stands in for it.
    if (orderItem.$manipulatorTarget && orderItem.$manipulatorTarget[0]) {
      orderItem.$manipulatorTarget[0].style.display = 'none';
    }
    var host = orderItem.$element[0];
    var NAMES = { '2': 'Hourly', '3': 'Daily', '4': 'Sun & Moon', '5': 'Wind',
                  '6': 'UV', '7': 'Air quality', '8': 'Details', '9': 'Forecast' };
    var ALL = ['2', '3', '4', '5', '6', '7', '8', '9'];
    var list = document.createElement('div');
    host.appendChild(list);

    // Sanitise the stored CSV into a full, deduped permutation of the eight ids.
    function currentOrder() {
      var raw = String(orderItem.get() || '').split(',');
      var seen = {}, out = [];
      for (var i = 0; i < raw.length; i++) {
        var k = raw[i].trim();
        if (NAMES[k] && !seen[k]) { seen[k] = 1; out.push(k); }
      }
      for (var j = 0; j < ALL.length; j++) if (!seen[ALL[j]]) out.push(ALL[j]);
      return out;
    }
    function save(order) { try { orderItem.set(order.join(',')); } catch (e) {} }
    function move(i, d) {
      var order = currentOrder(), j = i + d;
      if (j < 0 || j >= order.length) return;
      var t = order[i]; order[i] = order[j]; order[j] = t;
      save(order); render();
    }
    function render() {
      var order = currentOrder();
      save(order);                                     // persist the sanitised value
      list.innerHTML = '';
      for (var i = 0; i < order.length; i++) {
        (function (i) {
          var row = document.createElement('div');
          row.style.display = 'flex';
          row.style.alignItems = 'center';
          row.style.justifyContent = 'space-between';
          row.style.padding = '8px 12px';
          row.style.borderBottom = '1px solid #333';
          var name = document.createElement('span');
          name.textContent = NAMES[order[i]];
          var btns = document.createElement('span');
          var up = document.createElement('button');
          up.type = 'button'; up.textContent = '↑'; up.disabled = (i === 0);
          up.onclick = function () { move(i, -1); };
          var dn = document.createElement('button');
          dn.type = 'button'; dn.textContent = '↓'; dn.disabled = (i === order.length - 1);
          dn.onclick = function () { move(i, 1); };
          up.style.margin = dn.style.margin = '0 3px';
          btns.appendChild(up); btns.appendChild(dn);
          row.appendChild(name); row.appendChild(btns);
          list.appendChild(row);
        })(i);
      }
    }
    render();
  });
}

module.exports = { config: config, customFn: customFn };
