// Clay settings schema for the Aura Essential watchface.
// The three top complication slots share one list of contents (values match the
// C enum in aura-essential.c: 0 none, 1 steps, 2 heart rate, 3 battery, 4 day,
// 5 weather). BGCOLOR picks one of the curated background themes (index matches
// the THEMES table in the C).
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
  { "label": "Black", "value": "6" }
];

module.exports = [
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Appearance" },
      { "type": "select", "messageKey": "BGCOLOR", "label": "Background colour", "defaultValue": "0", "options": COLOR_OPTIONS }
    ]
  },
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
