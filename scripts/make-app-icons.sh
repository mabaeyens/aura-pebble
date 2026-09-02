#!/usr/bin/env bash
# Generate the Aura Essential appstore/dashboard icons by downscaling the real
# Aura brand icon (the white "A" whose swish crosses from the lower-left leg and
# curls into a wave on the right, over a blue gradient with a warm corner glow).
# The 1024x1024 master lives at docs/store/aura-brand-icon.png (Aura's own asset,
# also served at https://askmira.es/aura/assets/icon.png). Downscaled with Lanczos
# to the two sizes the developer dashboard asks for:
#   Small Icon  80x80   -> docs/store/app-icon-80.png
#   Large Icon  144x144 -> docs/store/app-icon-144.png
# This icon is dashboard-only, so it does not need a Pebble-specific redraw.
# Requires ImageMagick 7 (`magick`). Re-run to regenerate.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

MASTER="docs/store/aura-brand-icon.png"

magick "$MASTER" -filter Lanczos -resize 144x144 docs/store/app-icon-144.png
magick "$MASTER" -filter Lanczos -resize 80x80   docs/store/app-icon-80.png

echo "wrote docs/store/app-icon-144.png and docs/store/app-icon-80.png"
