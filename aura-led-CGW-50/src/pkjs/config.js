var pkg = require('../../package.json');

// Clay settings schema for the Aura LCD watchface.
// Values are strings that the C parses as ints (see tuple_int in aura-led-cgw-50.c).
//  - THEME: 0 automatic (negative after sunset), 1 always day, 2 always night.
//  - PRESET: 0 custom, 1-12 pre-built themes, 13 follows the weather (table in the C).
//  - DAY_* / NIGHT_* pick background and digit colours for each theme; indices match
//    palette() in the C. If a pick leaves too little contrast the C falls back to
//    black or white digits, so a bad choice can never hide the time.
//  - ACCENT colours the rules, weekday, daylight bar, weather icon and Halley's comet;
//    "-1" follows the Aura sky ramp by hour.
var COLOR_OPTIONS = [
  { "label": "White", "value": "0" },
  { "label": "Black", "value": "1" },
  { "label": "Light grey", "value": "2" },
  { "label": "Dark grey", "value": "3" },
  { "label": "Red", "value": "4" },
  { "label": "Orange", "value": "5" },
  { "label": "Yellow", "value": "6" },
  { "label": "Green", "value": "7" },
  { "label": "Cyan", "value": "8" },
  { "label": "Blue", "value": "9" },
  { "label": "Purple", "value": "10" },
  { "label": "LCD yellow", "value": "11" },
  { "label": "Amber", "value": "12" }
];

var THEME_OPTIONS = [
  { "label": "Automatic (night after sunset)", "value": "0" },
  { "label": "Always day", "value": "1" },
  { "label": "Always night", "value": "2" }
];

var PRESET_OPTIONS = [
  { "label": "Custom (colours below)", "value": "0" },
  { "label": "Classic LCD", "value": "1" },
  { "label": "High contrast", "value": "2" },
  { "label": "Black on yellow (most readable)", "value": "3" },
  { "label": "Paper and ink", "value": "4" },
  { "label": "Sand and amber", "value": "5" },
  { "label": "Sky", "value": "6" },
  { "label": "Mood: sunny", "value": "7" },
  { "label": "Mood: rain", "value": "8" },
  { "label": "Mood: storm", "value": "9" },
  { "label": "Mood: snow", "value": "10" },
  { "label": "Mood: fog", "value": "11" },
  { "label": "Mood: sunset", "value": "12" },
  { "label": "Follow the weather", "value": "13" }
];

var ACCENT_OPTIONS = [{ "label": "Automatic (sky colour by hour)", "value": "-1" }].concat(COLOR_OPTIONS);

// The weather location is a worldwide city search (SEARCH), not a fixed list:
// the typeahead in index.js queries the keyless Open-Meteo geocoder and fills
// CITY / LAT / LON on tap. USE_GPS off means "use the searched city / manual
// coordinates below"; on means the phone's GPS.
module.exports = [
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Layout" },
      { "type": "select", "messageKey": "LAYOUT", "label": "Style", "defaultValue": "0",
        "options": [{ "label": "Quiet (plain)", "value": "0" }, { "label": "Panel (LCD pane in a dark bezel)", "value": "1" },
          { "label": "Timeline (sun strip over forecast)", "value": "2" }, { "label": "Stack (hours over minutes)", "value": "3" }] }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Theme" },
      { "type": "text", "defaultValue": "Night turns the face negative from sunset to sunrise at the weather location." },
      { "type": "select", "messageKey": "THEME", "label": "Mode", "defaultValue": "0", "options": THEME_OPTIONS },
      { "type": "select", "messageKey": "PRESET", "label": "Colour theme", "defaultValue": "0", "options": PRESET_OPTIONS },
      { "type": "text", "defaultValue": "Every theme has a day and a night look. Follow the weather picks the mood from the current conditions. Anything other than Custom replaces the day and night colours below; an accent you pick still wins." }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Day colours" },
      { "type": "select", "messageKey": "DAY_BG", "label": "Background", "defaultValue": "0", "options": COLOR_OPTIONS },
      { "type": "select", "messageKey": "DAY_FG", "label": "Digits and lines", "defaultValue": "1", "options": COLOR_OPTIONS }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Night colours" },
      { "type": "select", "messageKey": "NIGHT_BG", "label": "Background", "defaultValue": "1", "options": COLOR_OPTIONS },
      { "type": "select", "messageKey": "NIGHT_FG", "label": "Digits and lines", "defaultValue": "12", "options": COLOR_OPTIONS }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Accent" },
      { "type": "text", "defaultValue": "Rules, weekday, daylight bar, weather icon and Halley's comet. Digits never use it." },
      { "type": "select", "messageKey": "ACCENT", "label": "Colour", "defaultValue": "-1", "options": ACCENT_OPTIONS }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Display" },
      { "type": "select", "messageKey": "DATE_STYLE", "label": "Date lettering (Quiet and Panel)", "defaultValue": "4",
        "options": [{ "label": "14-segment LCD", "value": "4" }, { "label": "Dot matrix 5 x 7", "value": "1" }, { "label": "Bold dots", "value": "2" }, { "label": "Dots + LCD day number", "value": "3" }, { "label": "Gothic", "value": "0" }] },
      { "type": "toggle", "messageKey": "TIME24", "label": "24 hour clock", "defaultValue": true },
      { "type": "toggle", "messageKey": "DATE_MDY", "label": "Month before day (off = day-month)", "defaultValue": true },
      { "type": "toggle", "messageKey": "GHOSTS", "label": "Show unlit LCD segments", "defaultValue": true }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Weather" },
      { "type": "text", "defaultValue": "Temperature, high and low, forecast, sunrise and sunset, from Open-Meteo (open-meteo.com, CC BY 4.0)." },
      { "type": "toggle", "messageKey": "UNITS", "label": "Fahrenheit (off = Celsius)", "defaultValue": false },
      { "type": "input", "messageKey": "SEARCH", "label": "Search a city",
        "attributes": { "placeholder": "e.g. Tokyo, Teruel", "autocomplete": "off" } },
      { "type": "input", "messageKey": "CITY", "label": "Selected",
        "attributes": { "placeholder": "none yet" } },
      { "type": "toggle", "messageKey": "USE_GPS", "label": "Use phone GPS (off = the city above)", "defaultValue": true },
      { "type": "text", "defaultValue": "Latitude / longitude fill in when you pick a city; edit them to set a location by hand." },
      { "type": "input", "messageKey": "LAT", "label": "Latitude", "attributes": { "placeholder": "40.4168", "type": "text" } },
      { "type": "input", "messageKey": "LON", "label": "Longitude", "attributes": { "placeholder": "-3.7038", "type": "text" } }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Credits" },
      { "type": "text", "defaultValue": "Inspired by the Casio CGW-50 Cosmo Phase. An independent, non-commercial tribute, not affiliated with or endorsed by Casio." },
      { "type": "text", "defaultValue": "Digit rendering adapted from PT2-LCD221 by Kicou (MIT). Segment shapes from the 7-Segment font by Jan Bobrowski and DSEG14 by Keshikan (SIL OFL 1.1). Weather icons by Erik Flowers (SIL OFL 1.1). Weather data by Open-Meteo.com (CC BY 4.0)." }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "text", "defaultValue": "Aura LCD v" + pkg.version,
        "attributes": { "style": "text-align:center;color:#8aa0b6;font-size:12px;" } }
    ]
  },
  { "type": "submit", "defaultValue": "Save" }
];
