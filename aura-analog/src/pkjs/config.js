// Clay settings schema for the Aura Analog watchface.
module.exports = [
  { "type": "heading", "defaultValue": "Aura Analog" },
  { "type": "text", "defaultValue": "A Swiss-railway stop-to-go face." },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Face" },
      { "type": "toggle", "messageKey": "THEME", "label": "Black dial (lower power)", "defaultValue": false },
      { "type": "toggle", "messageKey": "SECONDS", "label": "Second hand", "defaultValue": true },
      { "type": "toggle", "messageKey": "WORDMARK", "label": "AURA wordmark", "defaultValue": true },
      { "type": "toggle", "messageKey": "BOTTOM", "label": "Bottom dial shows heart rate (off = steps)", "defaultValue": false }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Weather (left dial)" },
      { "type": "toggle", "messageKey": "UNITS", "label": "Fahrenheit (off = Celsius)", "defaultValue": false },
      { "type": "toggle", "messageKey": "LOCMODE", "label": "Set location manually (off = GPS)", "defaultValue": false },
      { "type": "input", "messageKey": "LAT", "label": "Latitude", "attributes": { "placeholder": "40.4168", "type": "text" } },
      { "type": "input", "messageKey": "LON", "label": "Longitude", "attributes": { "placeholder": "-3.7038", "type": "text" } }
    ]
  },
  { "type": "submit", "defaultValue": "Save" }
];
