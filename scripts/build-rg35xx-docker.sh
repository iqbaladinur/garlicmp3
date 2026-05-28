#!/bin/sh
set -eu

TOOLCHAIN_DIR="$(dirname "$0")/../ref/rg35xx-toolchain"
IMAGE="aveferrum/rg35xx-toolchain"

# Build the image if not already built
if ! docker image inspect "$IMAGE" > /dev/null 2>&1; then
    echo "Building $IMAGE from $TOOLCHAIN_DIR ..."
    docker build -t "$IMAGE" "$TOOLCHAIN_DIR"
fi

docker run --rm \
  -v "$PWD:/src" \
  -w /src \
  "$IMAGE" \
  bash -lc '
    set -eu

    CC_BIN="$(command -v arm-miyoo-linux-uclibcgnueabi-gcc || true)"
    if [ -z "$CC_BIN" ]; then
        echo "arm-miyoo-linux-uclibcgnueabi-gcc not found" >&2
        exit 1
    fi

    SDL_CONFIG="$(find /opt/miyoo -name sdl-config 2>/dev/null | head -n 1)"
    if [ -z "$SDL_CONFIG" ]; then
        echo "sdl-config not found" >&2
        exit 1
    fi

    make clean
    make dist \
        CROSS_COMPILE=arm-miyoo-linux-uclibcgnueabi- \
        CC="$CC_BIN" \
        NO_BUNDLED_LIBS=1 \
        CFLAGS="-std=c99 -Wall -Wextra -Os -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0" \
        SDL_CFLAGS="$($SDL_CONFIG --cflags)" \
        SDL_LIBS="$($SDL_CONFIG --libs)"

    file dist/APPS/GarlicMP3/garlic-mp3-player || true
    echo "Built with: $CC_BIN"
    echo "SDL config: $SDL_CONFIG"
  '

# Get static mpg123 from Miyoo toolchain (dynamically linked mpg123 won't work on device)
echo "Copying static mpg123 from Miyoo toolchain..."
docker run --rm \
  -v "$PWD/dist/APPS/GarlicMP3:/out" \
  "nfriedly/miyoo-toolchain:latest" \
  sh -lc 'cp /opt/miyoo/arm-buildroot-linux-musleabi/sysroot/usr/bin/mpg123 /out/mpg123 && chmod +x /out/mpg123'

echo "Done."
find dist/APPS -type f -printf '%p %s bytes\n'
