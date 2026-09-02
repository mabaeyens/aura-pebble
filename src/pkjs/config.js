// Clay settings schema for the Aura Essential watchface.
// Values are strings that the C parses as ints (see tuple_int in aura-essential.c).
//  - SLOT1/2/3 pick each top complication (0 none, 1 steps, 2 heart rate,
//    3 battery, 4 day, 5 weather); matches the C_* enum.
//  - TOPCOLOR / BANDCOLOR / BOTCOLOR pick the three block colours; the C derives
//    black or white content from each block's luminance so text stays readable.
//  - TIMEFONT picks the clock face (matches the time_font() switch in the C).
//  - SEPCOLOR colours the separator line; "-1" turns it off.
var SLOT_OPTIONS = [
  { "label": "Nothing", "value": "0" },
  { "label": "Steps", "value": "1" },
  { "label": "Heart rate", "value": "2" },
  { "label": "Battery", "value": "3" },
  { "label": "Day of week", "value": "4" },
  { "label": "Weather", "value": "5" }
];

var COLOR_OPTIONS = [
  { "label": "Orange", "value": "0" },
  { "label": "Blue", "value": "1" },
  { "label": "Green", "value": "2" },
  { "label": "Yellow", "value": "3" },
  { "label": "Red", "value": "4" },
  { "label": "Purple", "value": "5" },
  { "label": "Black", "value": "6" },
  { "label": "White", "value": "7" },
  { "label": "Dark grey", "value": "8" },
  { "label": "Light grey", "value": "9" }
];

var FONT_OPTIONS = [
  { "label": "LECO (segmented)", "value": "0" },
  { "label": "Roboto (bold)", "value": "1" },
  { "label": "Bitham (display)", "value": "2" },
  { "label": "Gothic (plain)", "value": "3" }
];

var SEP_OPTIONS = [{ "label": "Off", "value": "-1" }].concat(COLOR_OPTIONS);

// Time digits can follow the band automatically or take a fixed colour.
var TIME_COLOR_OPTIONS = [{ "label": "Auto (by band)", "value": "-1" }].concat(COLOR_OPTIONS);

// City picker for weather. "value" is the index into the CITIES table in
// index.js (which holds the coordinates); keep the two lists in the same order.
// "-1" means use the phone's GPS.
var CITY_OPTIONS = [
  { "label": "GPS location", "value": "-1" },
  { "label": "Madrid, ES", "value": "0" },
  { "label": "Barcelona, ES", "value": "1" },
  { "label": "Valencia, ES", "value": "2" },
  { "label": "Sevilla, ES", "value": "3" },
  { "label": "Zaragoza, ES", "value": "4" },
  { "label": "Malaga, ES", "value": "5" },
  { "label": "Murcia, ES", "value": "6" },
  { "label": "Palma, ES", "value": "7" },
  { "label": "Las Palmas, ES", "value": "8" },
  { "label": "Bilbao, ES", "value": "9" },
  { "label": "Alicante, ES", "value": "10" },
  { "label": "Cordoba, ES", "value": "11" },
  { "label": "Valladolid, ES", "value": "12" },
  { "label": "Vigo, ES", "value": "13" },
  { "label": "Granada, ES", "value": "14" },
  { "label": "A Coruna, ES", "value": "15" },
  { "label": "Santander, ES", "value": "16" },
  { "label": "San Sebastian, ES", "value": "17" },
  { "label": "Santa Cruz de Tenerife, ES", "value": "18" },
  { "label": "Pamplona, ES", "value": "19" },
  { "label": "London, UK", "value": "20" },
  { "label": "Paris, FR", "value": "21" },
  { "label": "Lisbon, PT", "value": "22" }
];

module.exports = [
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Complications" },
      { "type": "text", "defaultValue": "Pick what each of the three top slots shows." },
      { "type": "select", "messageKey": "SLOT1", "label": "Left", "defaultValue": "1", "options": SLOT_OPTIONS },
      { "type": "select", "messageKey": "SLOT2", "label": "Middle", "defaultValue": "2", "options": SLOT_OPTIONS },
      { "type": "select", "messageKey": "SLOT3", "label": "Right", "defaultValue": "3", "options": SLOT_OPTIONS }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Colours" },
      { "type": "select", "messageKey": "TOPCOLOR", "label": "Top block", "defaultValue": "0", "options": COLOR_OPTIONS },
      { "type": "select", "messageKey": "BANDCOLOR", "label": "Time band", "defaultValue": "7", "options": COLOR_OPTIONS },
      { "type": "select", "messageKey": "BOTCOLOR", "label": "Bottom block", "defaultValue": "0", "options": COLOR_OPTIONS },
      { "type": "select", "messageKey": "COMPCOLOR", "label": "Complications", "defaultValue": "7", "options": COLOR_OPTIONS },
      { "type": "select", "messageKey": "TIMECOLOR", "label": "Time digits", "defaultValue": "-1", "options": TIME_COLOR_OPTIONS },
      { "type": "select", "messageKey": "SEPCOLOR", "label": "Separator", "defaultValue": "6", "options": SEP_OPTIONS }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Time" },
      { "type": "select", "messageKey": "TIMEFONT", "label": "Font", "defaultValue": "0", "options": FONT_OPTIONS }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Weather" },
      { "type": "text", "defaultValue": "Used by any slot set to Weather, from Open-Meteo." },
      { "type": "toggle", "messageKey": "UNITS", "label": "Fahrenheit (off = Celsius)", "defaultValue": false },
      { "type": "select", "messageKey": "CITY", "label": "City", "defaultValue": "-1", "options": CITY_OPTIONS },
      { "type": "text", "defaultValue": "Manual coordinates below override the city; the city overrides GPS." },
      { "type": "toggle", "messageKey": "LOCMODE", "label": "Set location manually (off = GPS/city)", "defaultValue": false },
      { "type": "input", "messageKey": "LAT", "label": "Latitude", "attributes": { "placeholder": "40.4168", "type": "text" } },
      { "type": "input", "messageKey": "LON", "label": "Longitude", "attributes": { "placeholder": "-3.7038", "type": "text" } }
    ]
  },
  { "type": "submit", "defaultValue": "Save" }
];
