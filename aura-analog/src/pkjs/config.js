// Clay settings schema for the Aura Analog watchface.
module.exports = [
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Face" },
      { "type": "toggle", "messageKey": "THEME", "label": "Black dial (lower power)", "defaultValue": false },
      { "type": "toggle", "messageKey": "SECONDS", "label": "Second hand", "defaultValue": true },
      { "type": "toggle", "messageKey": "WORDMARK", "label": "AURA wordmark", "defaultValue": true },
      {
        "type": "select",
        "messageKey": "BOTTOM",
        "label": "Bottom dial",
        "defaultValue": "0",
        "options": [
          { "label": "Battery", "value": "0" },
          { "label": "Steps", "value": "1" },
          { "label": "Heart rate", "value": "2" }
        ]
      }
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
