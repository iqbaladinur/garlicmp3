#!/bin/sh
set -eu

IMAGE="${MIYOO_IMAGE:-nfriedly/miyoo-toolchain:latest}"

docker run --rm \
  -v "$PWD:/src" \
  -w /src \
  "$IMAGE" \
  sh -lc '
    set -eu

    export PATH="/opt/miyoo/bin:$PATH"

    CC_BIN="$(command -v arm-linux-gcc || true)"
    if [ -z "$CC_BIN" ]; then
      echo "arm-linux-gcc not found in toolchain image" >&2
      exit 1
    fi

    TOOL_BIN="$(dirname "$CC_BIN")"
    SDL_CONFIG="$(find /opt/miyoo -type f -name sdl-config 2>/dev/null | head -n 1)"
    if [ -z "$SDL_CONFIG" ]; then
      echo "sdl-config not found in toolchain image" >&2
      exit 1
    fi

    export PATH="$TOOL_BIN:$PATH"
    make clean
    make dist \
      CROSS_COMPILE=arm-linux- \
      CC="$CC_BIN" \
      NO_BUNDLED_LIBS=1 \
      CFLAGS="-std=c99 -Wall -Wextra -Os -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0" \
      SDL_CFLAGS="$($SDL_CONFIG --cflags)" \
      SDL_LIBS="$($SDL_CONFIG --libs)"

    if [ -x /opt/miyoo/arm-buildroot-linux-musleabi/sysroot/usr/bin/mpg123 ]; then
      cp /opt/miyoo/arm-buildroot-linux-musleabi/sysroot/usr/bin/mpg123 dist/APPS/GarlicMP3/mpg123
      chmod +x dist/APPS/GarlicMP3/mpg123
    fi

    echo "Built with: $CC_BIN"
    echo "SDL config: $SDL_CONFIG"
    file dist/APPS/GarlicMP3/garlic-mp3-player || true
    file dist/APPS/GarlicMP3/mpg123 || true
    arm-linux-readelf -d dist/APPS/GarlicMP3/garlic-mp3-player | grep NEEDED || true
  '
