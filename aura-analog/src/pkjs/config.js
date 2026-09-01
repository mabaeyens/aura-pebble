// Clay settings schema for the Aura Analog watchface.
// The four subdial slots share one list of contents (values match the C enum in
// aura-analog.c: 0 none, 1 wordmark, 2 weather, 3 day, 4 date, 5 battery, 6 steps,
// 7 heart rate).
var SLOT_OPTIONS = [
  { "label": "Nothing", "value": "0" },
  { "label": "AURA wordmark", "value": "1" },
  { "label": "Weather", "value": "2" },
  { "label": "Day", "value": "3" },
  { "label": "Date", "value": "4" },
  { "label": "Battery", "value": "5" },
  { "label": "Steps", "value": "6" },
  { "label": "Heart rate", "value": "7" }
];

module.exports = [
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Face" },
      { "type": "toggle", "messageKey": "THEME", "label": "Black dial (lower power)", "defaultValue": false },
      { "type": "toggle", "messageKey": "SECONDS", "label": "Second hand", "defaultValue": true }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Complications" },
      { "type": "text", "defaultValue": "Pick what each of the four subdials shows." },
      { "type": "select", "messageKey": "TOP", "label": "Top dial (12)", "defaultValue": "1", "options": SLOT_OPTIONS },
      { "type": "select", "messageKey": "LEFT", "label": "Left dial (9)", "defaultValue": "2", "options": SLOT_OPTIONS },
      { "type": "select", "messageKey": "RIGHT", "label": "Right dial (3)", "defaultValue": "3", "options": SLOT_OPTIONS },
      { "type": "select", "messageKey": "BOTTOM", "label": "Bottom dial (6)", "defaultValue": "5", "options": SLOT_OPTIONS }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Weather" },
      { "type": "text", "defaultValue": "Shown on the left dial, from Open-Meteo." },
      { "type": "toggle", "messageKey": "UNITS", "label": "Fahrenheit (off = Celsius)", "defaultValue": false },
      { "type": "toggle", "messageKey": "LOCMODE", "label": "Set location manually (off = GPS)", "defaultValue": false },
      { "type": "input", "messageKey": "LAT", "label": "Latitude", "attributes": { "placeholder": "40.4168", "type": "text" } },
      { "type": "input", "messageKey": "LON", "label": "Longitude", "attributes": { "placeholder": "-3.7038", "type": "text" } }
    ]
  },
  { "type": "submit", "defaultValue": "Save" }
];
