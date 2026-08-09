#!/bin/bash
# Render garlicmp3 UI previews from the REAL ui_sdl.c (no device needed).
# Usage: scripts/preview-ui.sh [outdir]   (default /tmp/uipreview)
# Outputs: 1_library.png, 2_settings.png, 3_help.png
set -e

REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-/tmp/uipreview}"
mkdir -p "$OUT"

# One container run: (re)build static SDL 1.2.15 once (cached in named volume),
# compile the preview harness against the real UI code, render to BMP.
podman run --rm \
  -v "$REPO:/src:ro" \
  -v "$OUT:/out" \
  -v sdl15-preview:/tmp/sdl \
  localhost/aveferrum/rg35xx-toolchain:latest sh -c '
set -e
if [ ! -f /tmp/sdl/lib/libSDL.a ]; then
  echo "== building static SDL 1.2.15 (first run only) =="
  cd /tmp
  wget -q https://www.libsdl.org/release/SDL-1.2.15.tar.gz
  tar xzf SDL-1.2.15.tar.gz
  cd SDL-1.2.15
  ./configure --prefix=/tmp/sdl --disable-shared --enable-static \
    --disable-x11 --disable-video-opengl --disable-alsa --disable-pulseaudio \
    --disable-arts --disable-esd --disable-nasm >/dev/null 2>&1
  make -j4 >/dev/null 2>&1
  make install >/dev/null 2>&1
fi
echo "== compiling harness =="
gcc -O2 -o /tmp/preview /src/tools/preview.c /src/src/settings.c /src/src/ui_sdl.c \
  -I/tmp/sdl/include -I/src/src -L/tmp/sdl/lib -lSDL -lm
echo "== rendering =="
cd /out && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy /tmp/preview
'

echo "== converting to PNG =="
python3 - "$OUT" <<'PY'
import os, sys
from PIL import Image
out = sys.argv[1]
for name in ['1_library', '2_settings', '3_help']:
    bmp = os.path.join(out, name + '.bmp')
    png = os.path.join(out, name + '.png')
    if os.path.exists(bmp):
        Image.open(bmp).convert('RGB').save(png)
        print('->', png)
PY
