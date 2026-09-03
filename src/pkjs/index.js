// Aura Weather - phone-side bridge.
// Step 1 is a hard-coded on-watch demo, so this is only a placeholder entry
// point. Step 2 (docs/01-data-bridge.md) fills it in: fetch Open-Meteo, normalise
// to the 10-value code set, and ship the forecast in framed AppMessages.

Pebble.addEventListener('ready', function () {
  console.log('Aura Weather: PebbleKit JS ready (bridge not wired yet)');
});
