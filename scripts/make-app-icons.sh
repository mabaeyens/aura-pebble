#!/usr/bin/env bash
# Generate the Aura Essential appstore/dashboard icons: the Aura "A + swish"
# mark, white on the classic face's coral (#E66E6B, sampled from the top block of
# docs/store/essential-classic.png). Rendered at 576x576 then downscaled with
# Lanczos to the two sizes the developer dashboard asks for:
#   Small Icon  80x80   -> docs/store/app-icon-80.png
#   Large Icon  144x144 -> docs/store/app-icon-144.png
# Requires ImageMagick 7 (`magick`). Re-run to regenerate.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

FONT="aura-analog/resources/fonts/LiberationSans-Bold.ttf"
CORAL="#E66E6B"
MASTER="$(mktemp -d)/icon-master.png"

magick -size 576x576 "xc:${CORAL}" \
  -font "$FONT" -fill white -gravity center -pointsize 380 -annotate +0-30 "A" \
  -draw "fill none stroke white stroke-width 34 stroke-linecap round stroke-linejoin round \
         path 'M 30,410 C 124,372 220,372 314,402 C 402,430 470,424 530,388 C 578,360 585,314 555,300 C 535,290 520,304 527,324'" \
  "$MASTER"

magick "$MASTER" -filter Lanczos -resize 144x144 docs/store/app-icon-144.png
magick "$MASTER" -filter Lanczos -resize 80x80   docs/store/app-icon-80.png

echo "wrote docs/store/app-icon-144.png and docs/store/app-icon-80.png"
