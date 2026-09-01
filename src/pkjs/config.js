// Clay settings schema for the Aura Essential watchface.
// Values are strings that the C parses as ints (see tuple_int in aura-essential.c).
//  - SLOT1/2/3 pick each top complication (0 none, 1 steps, 2 heart rate,
//    3 battery, 4 day, 5 weather); matches the C_* enum.
//  - TOPCOLOR / BANDCOLOR pick the two block colours; the C derives black or
//    white content from each block's luminance so text stays readable.
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
      { "type": "toggle", "messageKey": "LOCMODE", "label": "Set location manually (off = GPS)", "defaultValue": false },
      { "type": "input", "messageKey": "LAT", "label": "Latitude", "attributes": { "placeholder": "40.4168", "type": "text" } },
      { "type": "input", "messageKey": "LON", "label": "Longitude", "attributes": { "placeholder": "-3.7038", "type": "text" } }
    ]
  },
  { "type": "submit", "defaultValue": "Save" }
];
