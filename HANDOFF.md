# Garlic MP3 Player Handoff

## Goal

Build a minimal standalone MP3 player app for Anbernic RG35XX original/OG batch 1 on GarlicOS 1.4.9.

Target behavior:

- App launcher directly under `Roms/APPS`.
- Binary and assets under `Roms/APPS/GarlicMP3`.
- Scan MP3 files from `Roms/MUSIC` first, with app-local `MUSIC` fallback.
- Simple 640x480 UI.
- Play/pause, stop, previous/next, volume, quit.
- Prioritize running on device over UI polish.

Current install layout:

```text
SD2:/Roms/APPS/GarlicMP3.sh
SD2:/Roms/APPS/GarlicMP3/garlic-mp3-player
SD2:/Roms/APPS/GarlicMP3/mpg123
SD2:/Roms/APPS/GarlicMP3/MUSIC/
SD2:/Roms/MUSIC/
```

## Repo State

Important files:

```text
Makefile
APPS/GarlicMP3.sh
src/main.c
src/input.c
src/file_scan.c
src/audio_mpg123.c
src/ui_sdl.c
scripts/build-miyoo-docker.sh
third_party/sdl12-min/SDL/SDL.h
ref/Drastic.sh
ref/drastic/
garlic.img
```

Build command that currently works:

```sh
make docker-miyoo-dist
```

Output:

```text
dist/APPS/
```

The Docker image used:

```text
nfriedly/miyoo-toolchain:latest
```

The build script copies a static `mpg123` from the Docker image into the app folder.

## Build/Runtime Findings

Host `arm-linux-gnueabihf-gcc` failed on device due modern glibc requirements:

```text
GLIBC_2.33 not found
GLIBC_2.34 not found
```

`nfriedly/miyoo-toolchain:latest` produces static ARM binaries:

```text
garlic-mp3-player: ELF 32-bit LSB executable, ARM, statically linked
mpg123: ELF 32-bit LSB executable, ARM, statically linked
```

This fixed the earlier dynamic loader/glibc crash.

`garlic.img` contains `libSDL-1.2.so.0.11.4`, but not SDL 1.2 development headers. A minimal SDL 1.2 header was added under `third_party/sdl12-min`, but the current working Docker build uses the SDL headers/libs from the Miyoo toolchain instead.

## Current Functional Status

Confirmed working:

- Launcher is detected by GarlicOS when `GarlicMP3.sh` is directly under `Roms/APPS`.
- App starts and returns to GarlicOS cleanly via auto-timeout.
- UI renders.
- MP3 scan works.
- `Roms/MUSIC` scan works: latest logs show `scan done tracks=18`.
- `mpg123` static is bundled in the app folder.

Still broken:

- Input responsiveness is not acceptable.
- D-pad sometimes moves once, then appears unresponsive/random.
- A/play has not been reliably tested because input is not stable.

Debug behavior:

- Auto-quit is currently enabled at 60 seconds in `src/main.c`.
- Logs are written to `Roms/APPS/GarlicMP3/garlic-mp3.log`.

## Important Logs / Observations

Initial working UI/static build log:

```text
main start
Joystick: RG35XX Gamepad buttons=12 axes=6 hats=1
Input event device: /dev/input/event0 name=atc260x_onoff
Input event device: /dev/input/event1 name=RG35XX Gamepad
input init done
ui init done
scan done tracks=18 truncated=0
```

Raw `/dev/input/event*` attempt:

```text
Input mode: raw /dev/input only
Input event device: /dev/input/event0 name=atc260x_onoff
Input event device: /dev/input/event1 name=RG35XX Gamepad
...
Heartbeat selected=0 tracks=18 state=0
Auto quit after 60000 ms
```

No `EV event...` lines appeared even after pressing buttons. Conclusion: direct raw input reads are not useful in this app context, despite devices opening successfully.

SDL event path previously produced input:

```text
SDL joy hat=0 value=4
Handle action=2 selected=0 tracks=18
SDL poll hat value=4
Handle action=2 selected=1 tracks=18
```

This showed a bug at the time: SDL event queue and manual joystick polling were both processing the same input, causing double actions and random-looking movement.

After switching to SDL event only with Drastic-style mapping, latest user log:

```text
Input mode: SDL joystick events using Drastic-style mapping
Joystick: RG35XX Gamepad buttons=12 axes=6 hats=1
scan done tracks=18 truncated=0
Heartbeat selected=0 tracks=18 state=0
Action=2 selected=0 tracks=18
Heartbeat selected=1 tracks=18 state=0
...
Auto quit after 60000 ms
```

This means at least one D-pad down action was received and selected moved from `0` to `1`, but subsequent inputs did not work reliably.

## Ref Drastic Findings

The reference app is under:

```text
ref/Drastic.sh
ref/drastic/
```

Drastic launcher:

```sh
HOME=$progdir \
ALSA_CONFIG_DIR="${progdir}/libs/alsa" \
LD_LIBRARY_PATH="${progdir}/libs" \
SDL_VIDEODRIVER=directfb \
SDL_AUDIODRIVER=alsa \
./drastic ${GAME:+"$GAME"} &> log.txt
```

Drastic binary:

```text
ELF 32-bit ARM, dynamically linked, interpreter libs/ld-linux-armhf.so.3
RUNPATH: libs
NEEDED: libSDL2-2.0.so.0, libasound.so.2, libc.so.6, etc.
```

Important: Drastic uses SDL2, not SDL 1.2.

Drastic config contains useful input mapping:

```text
controls_b[CONTROL_INDEX_UP] = 1089
controls_b[CONTROL_INDEX_DOWN] = 1092
controls_b[CONTROL_INDEX_LEFT] = 1096
controls_b[CONTROL_INDEX_RIGHT] = 1090
controls_b[CONTROL_INDEX_A] = 1025
controls_b[CONTROL_INDEX_B] = 1024
controls_b[CONTROL_INDEX_X] = 1027
controls_b[CONTROL_INDEX_Y] = 1026
controls_b[CONTROL_INDEX_L] = 1028
controls_b[CONTROL_INDEX_R] = 1029
controls_b[CONTROL_INDEX_START] = 1030
controls_b[CONTROL_INDEX_SELECT] = 1031
```

This suggests Drastic encodes joystick events as:

```text
button code = 1024 + SDL joystick button index
hat code roughly = 1024 + hat/direction encoded values
```

The current app attempted to mimic this at the SDL 1.2 event level, but behavior remains poor.

## Current Input Implementation Notes

`src/input.c` has accumulated several experiments:

- SDL 1.2 joystick event handling.
- Manual `SDL_JoystickGetButton/GetHat/GetAxis` polling.
- Raw `/dev/input/event*` fallback.
- Drastic-style mapping attempt.

This file likely needs cleanup.

Current mode at handoff:

- `SDL_INIT_VIDEO | SDL_INIT_JOYSTICK` is enabled in `src/main.c`.
- `input_poll_device()` is no longer called from the main loop.
- `input_event_to_action()` handles SDL joystick events.
- D-pad hat code was just patched to use bitmask checks rather than exact equality, because RG35XX emits combined/transitional hat values like `12` and `6`.

The latest build after that patch may not yet have been tested by the user.

## Recommended Next Steps

Most pragmatic path:

1. Stop trying to perfect SDL 1.2 input if the latest bitmask patch still fails.
2. Port the app to SDL2 using the exact bundled SDL2 stack from `ref/drastic/libs`.
3. Link dynamically like Drastic:

```text
interpreter: libs/ld-linux-armhf.so.3
RUNPATH: libs
LD_LIBRARY_PATH="$PWD/libs"
SDL_VIDEODRIVER=directfb
SDL_AUDIODRIVER=alsa
ALSA_CONFIG_DIR="$PWD/libs/alsa"
```

4. Reuse Drastic-style SDL2 joystick mapping:

```text
button 0 = B? from Drastic UI_EXIT
button 1 = A? verify against config/log
button 2 = Y/X depending physical order
D-pad via SDL2 hat events
L/R/start/select from button indexes 4..7 or Drastic config values
```

Alternative if staying SDL 1.2:

- Use only one input path at a time.
- Prefer SDL joystick event queue.
- Add concise logs for every SDL event type/value:

```text
JOYHAT value
JOYBUTTON button down/up
JOYAXIS axis/value
```

- Do not combine event queue and manual polling in the same build unless deduplicated.
- Keep auto-quit enabled during debugging.

## Known Gotchas

- `.sh` must be directly under `Roms/APPS`; nested `.sh` is not detected by GarlicOS.
- Windows Explorer cannot copy Linux symlinks to SD card. Use real file copies, not symlinks.
- `mpg123` was missing on device. Bundle the static `mpg123` from Miyoo toolchain.
- SD card setup is 2-card: use SD2 `/Roms/APPS` and `/Roms/MUSIC`.
- Raw `/dev/input/event1` opened but produced no events in this process.
- Excessive logging to SD card can make UI feel worse; keep logs concise after mapping is known.

## Useful Commands

Build current package:

```sh
make docker-miyoo-dist
```

Inspect output:

```sh
file dist/APPS/GarlicMP3/garlic-mp3-player
file dist/APPS/GarlicMP3/mpg123
find dist/APPS -maxdepth 3 -type f -printf '%p %s bytes\n'
```

Install by copying:

```text
dist/APPS/* -> SD2:/Roms/APPS/
```

Test log location:

```text
SD2:/Roms/APPS/GarlicMP3/garlic-mp3.log
```

