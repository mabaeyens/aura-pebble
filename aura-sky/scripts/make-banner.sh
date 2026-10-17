#!/usr/bin/env bash
# Optional 720x320 marketing banner for the dashboard (the size is from third-party
# docs; the official Pebble docs only say the banner is optional). Three real emulator
# shots on simple watch frames over the golden-hour gradient, plus the title and a
# one-line headline. Output: docs/store/sky-banner.png. Requires ImageMagick 7.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
S=docs/store
FONT=aura-sky/resources/fonts/AtkinsonHyperlegibleNext-Bold.otf
WORK=$(mktemp -d)

magick -size 1x5 xc:'#241f6b' -fill '#5b2a86' -draw 'point 0,1' -fill '#b83f7c' -draw 'point 0,2' \
  -fill '#f2724f' -draw 'point 0,3' -fill '#ffb347' -draw 'point 0,4' \
  -filter Triangle -resize 720x320! "$WORK/bg.png"

# A framed watch: screenshot with a dark rounded bezel.
frame() {  # in out
  magick "$1" -bordercolor '#111' -border 10 \
    \( +clone -alpha extract -draw 'fill black polygon 0,0 0,18 18,0 fill white circle 18,18 18,0' \
       \( +clone -flip \) -compose Multiply -composite \( +clone -flop \) -compose Multiply -composite \) \
    -alpha off -compose CopyOpacity -composite "$2"
}
frame $S/sky-rainy-dusk.png "$WORK/f1.png"
frame $S/sky-golden-hour.png "$WORK/f2.png"
frame $S/sky-snow.png "$WORK/f3.png"
for i in 1 2 3; do magick "$WORK/f$i.png" -resize 74% "$WORK/g$i.png"; done

magick "$WORK/bg.png" \
  "$WORK/g3.png" -geometry +545+70 -composite \
  "$WORK/g1.png" -geometry +352+70 -composite \
  "$WORK/g2.png" -geometry +450+34 -composite \
  -font "$FONT" -fill white -pointsize 44 -annotate +36+130 'Aura Sky' \
  -pointsize 44 -annotate +36+180 'Weather' \
  -pointsize 19 -fill '#fff1d0' -annotate +36+222 'The sky outside,' \
  -pointsize 19 -annotate +36+248 'painted on your wrist.' \
  -pointsize 14 -fill '#3a1a5a' -annotate +36+290 'Pebble Time 2  |  Free and open source' \
  docs/store/sky-banner.png
rm -rf "$WORK"
echo "wrote docs/store/sky-banner.png"
