#!/usr/bin/env bash
# Generate the Aura Sky Weather store assets with ImageMagick 7 (`magick`):
#   docs/store/sky-app-icon-144.png   large icon, 144x144
#   docs/store/sky-app-icon-80.png    small icon, 80x80
#   docs/store/sky-menu-icon.png      launcher icon, 25x25 white on transparent
#                                     (also copied to aura-sky/resources/images/menu-icon.png)
# The app icon is a golden-hour sky over two hills, the same scene the app paints,
# drawn at 1024 and downscaled with Lanczos. Re-run to regenerate.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
OUT=docs/store
WORK=$(mktemp -d)

# Sky: a five-stop vertical gradient (indigo, purple, magenta, orange, amber), interpolated.
magick -size 1x5 xc:'#241f6b' \
  -fill '#5b2a86' -draw 'point 0,1' \
  -fill '#b83f7c' -draw 'point 0,2' \
  -fill '#f2724f' -draw 'point 0,3' \
  -fill '#ffb347' -draw 'point 0,4' \
  -filter Triangle -resize 1024x1024! "$WORK/sky.png"

# Sun glow, then the sun, sitting on the horizon.
magick -size 1024x1024 xc:none -fill '#ffd36b' -draw 'circle 512,690 512,330' \
  -blur 0x70 -channel A -evaluate multiply 0.75 +channel "$WORK/glow.png"
magick -size 1024x1024 xc:none -fill '#fff1a8' -draw 'circle 512,690 512,500' "$WORK/sun.png"

# Two clouds, soft white.
magick -size 1024x1024 xc:none -fill white \
  -draw 'roundrectangle 130,250 400,330 40,40' -draw 'circle 230,250 230,190' -draw 'circle 310,238 310,178' \
  -draw 'roundrectangle 640,160 880,226 33,33' -draw 'circle 730,160 730,108' -draw 'circle 795,152 795,104' \
  -channel A -evaluate multiply 0.92 +channel "$WORK/clouds.png"

# Hills: back (mid green) and front (deep green), polygons.
magick -size 1024x1024 xc:none -fill '#3a6b46' \
  -draw 'polygon 0,720 150,690 330,740 520,700 720,745 900,700 1024,730 1024,1024 0,1024' "$WORK/hill-back.png"
magick -size 1024x1024 xc:none -fill '#1d3b2b' \
  -draw 'polygon 0,810 180,770 400,830 600,790 820,840 1024,800 1024,1024 0,1024' "$WORK/hill-front.png"

magick "$WORK/sky.png" "$WORK/glow.png" -composite "$WORK/sun.png" -composite \
  "$WORK/clouds.png" -composite "$WORK/hill-back.png" -composite "$WORK/hill-front.png" -composite \
  "$WORK/master.png"

magick "$WORK/master.png" -filter Lanczos -resize 144x144 "$OUT/sky-app-icon-144.png"
magick "$WORK/master.png" -filter Lanczos -resize 80x80   "$OUT/sky-app-icon-80.png"

# Menu icon, drawn on the 25x25 pixel grid (no antialiasing): a half sun with rays above
# a horizon line, and a short reflection below. White on transparent; the launcher tints it.
magick -size 25x25 xc:none -fill white -stroke none \
  -draw 'point 12,1' -draw 'point 12,2' -draw 'point 12,3' -draw 'point 12,4' \
  -draw 'point 4,5' -draw 'point 5,6' -draw 'point 6,7' -draw 'point 7,8' -draw 'point 20,5' -draw 'point 19,6' -draw 'point 18,7' -draw 'point 17,8' \
  -draw 'point 2,13' -draw 'point 3,13' -draw 'point 4,13' -draw 'point 20,13' -draw 'point 21,13' -draw 'point 22,13' \
  -draw 'rectangle 8,13 16,13' -draw 'rectangle 7,14 17,14' -draw 'rectangle 6,15 18,15' -draw 'rectangle 6,16 18,16' \
  -draw 'rectangle 1,17 23,18' \
  -draw 'rectangle 5,21 19,21' -draw 'rectangle 8,23 16,23' \
  +antialias "$OUT/sky-menu-icon.png"
cp "$OUT/sky-menu-icon.png" aura-sky/resources/images/menu-icon.png
rm -rf "$WORK"
echo "wrote sky-app-icon-144.png, sky-app-icon-80.png, sky-menu-icon.png in $OUT"
