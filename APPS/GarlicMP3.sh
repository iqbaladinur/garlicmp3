#!/bin/sh

BASE_DIR="$(dirname "$0")"
APP_DIR="$BASE_DIR/GarlicMP3"
cd "$APP_DIR" || exit 1

LOG="./garlic-mp3.log"

{
  echo "=== GarlicMP3 launch ==="
  date
  echo "pwd: $(pwd)"
  echo "uname: $(uname -a)"
  echo "PATH before: $PATH"
  echo "LD_LIBRARY_PATH before: $LD_LIBRARY_PATH"
  echo "files:"
  ls -la
} >> "$LOG" 2>&1

export SDL_NOMOUSE=1
export SDL_VIDEO_CENTERED=1
export PATH="$PWD:$PATH"
export HOME="$PWD"
export LD_LIBRARY_PATH="$PWD/libs:$PWD:/lib:/usr/lib:/usr/local/lib:/usr/local/lib/arm-linux-gnueabihf:$LD_LIBRARY_PATH"
export SDL_AUDIODRIVER=alsa

if [ -f ./libs/libSDL-1.2.so.0.11.4 ] && [ ! -e ./libs/libSDL-1.2.so.0 ]; then
  cp ./libs/libSDL-1.2.so.0.11.4 ./libs/libSDL-1.2.so.0 2>/dev/null
fi

chmod +x ./garlic-mp3-player 2>/dev/null

{
  echo "PATH after: $PATH"
  echo "LD_LIBRARY_PATH after: $LD_LIBRARY_PATH"
  echo "binary:"
  ls -l ./garlic-mp3-player
  echo "start binary"
} >> "$LOG" 2>&1

./garlic-mp3-player >> "$LOG" 2>&1
RC=$?

{
  echo "binary exit: $RC"
  echo "=== GarlicMP3 end ==="
} >> "$LOG" 2>&1

exit $RC
