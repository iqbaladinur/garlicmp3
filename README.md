# Garlic MP3 Player

Minimal MP3 player skeleton for the original Anbernic RG35XX / RG35XX OG running GarlicOS 1.4.9.

This first version is intentionally small:

- Standalone GarlicOS APPS folder app.
- SDL 1.2 UI and input.
- MP3 playback through a command-line `mpg123` subprocess.
- Music scan from `/mnt/mmc/MUSIC`, `./MUSIC`, and the app-local `MUSIC` folder.
- For GarlicOS 2-SD setup, `Roms/MUSIC` beside `Roms/APPS` is scanned first.
- No GarlicOS 2, MuOS, Knulli, RG35XX Plus/H/2024, or H700 target assumptions.

## Build

You need an ARM Linux GCC toolchain and SDL 1.2 headers/libs for the target environment.

Default cross build:

```sh
make
```

The default compiler prefix is:

```text
arm-linux-gnueabihf-
```

Override it if your toolchain uses a different prefix:

```sh
make CROSS_COMPILE=arm-linux-
```

Build with an explicit sysroot:

```sh
make SYSROOT=/path/to/rg35xx/sysroot SDL_CFLAGS="-I/path/to/sysroot/usr/include/SDL" SDL_LIBS="-L/path/to/sysroot/usr/lib -lSDL"
```

Host syntax build, useful only for development on a Linux desktop with SDL 1.2 installed:

```sh
make CROSS_COMPILE= SDL_CFLAGS="$(sdl-config --cflags)" SDL_LIBS="$(sdl-config --libs)"
```

## Build Using `garlic.img`

If `garlic.img` is available in this repository root, the Makefile can extract the runtime libraries needed from the GarlicOS `SYSTEM.img` partition:

```sh
make extract-garlic-sysroot
make garlic-img-build
make garlic-img-dist
```

This creates an ignored local folder:

```text
garlic-sysroot/
```

The GarlicOS image includes runtime `libSDL-1.2.so.0.11.4`, but not SDL development headers. This repo therefore includes a small compatibility header in `third_party/sdl12-min/SDL/SDL.h` for the subset of SDL 1.2 used by this app.

The generated binary uses the GarlicOS glibc loader:

```text
/usr/local/lib/ld-linux-armhf.so.3
```

## Build Using Miyoo Toolchain Docker

For GarlicOS/RG35XX community builds, `nfriedly/miyoo-toolchain` is commonly used. This avoids linking against a modern host glibc.

Run with Docker Desktop or a running Docker daemon:

```sh
make docker-miyoo-dist
```

The script uses:

```text
nfriedly/miyoo-toolchain:latest
```

Override the image if needed:

```sh
MIYOO_IMAGE=nfriedly/miyoo-toolchain:steward make docker-miyoo-dist
```

Use `latest` first. Try `steward` only if the resulting binary still fails on device.

Create the APPS package:

```sh
make dist
```

Output:

```text
dist/APPS/
  GarlicMP3.sh
  GarlicMP3/
    garlic-mp3-player
    MUSIC/
    README.txt
```

## Install To SD Card

Copy the generated folder to the APPS directory on the ROMS partition:

```text
/Roms/APPS/GarlicMP3.sh
/Roms/APPS/GarlicMP3/garlic-mp3-player
/Roms/APPS/GarlicMP3/MUSIC/
```

Put MP3 files in either:

```text
/Roms/MUSIC
/mnt/mmc/MUSIC
/Roms/APPS/GarlicMP3/MUSIC
```

The app scans one subdirectory level below those folders.

## Controls

Keyboard fallback:

- Up/Down: select track
- Enter/Space: play selected
- Escape/Backspace: stop
- P: pause/resume
- Left/Right: previous/next
- Minus/Equals: volume down/up
- Q: quit

RG35XX SDL joystick defaults:

- D-pad up/down: select track
- A: play selected
- B: stop
- Start: pause/resume
- L/R: previous/next
- X/Y: volume down/up
- Select+Start or Menu: quit

Debug build note: current builds auto-quit after about 60 seconds so failed input tests do not require a forced reboot.

Button IDs can vary by SDL/device build. Unknown joystick buttons are written to `garlic-mp3.log`.

## Runtime Dependencies

The player expects `mpg123` to be available in `PATH` on GarlicOS. The stock `SYSTEM.img` inspected for this skeleton did not include `mpg123` or `amixer`. If GarlicOS does not provide them on your SD card, copy compatible ARM binaries into the app folder. The launcher already prepends the app folder to `PATH`:

```sh
export PATH="$PWD:$PATH"
```

Volume uses `amixer` and tries both `Master` and `PCM`. If neither control exists, playback should continue but volume buttons may do nothing.

## Troubleshooting GarlicOS 1.4.9

- App does not appear: verify the folder is under `/Roms/APPS/` and `GarlicMP3.sh` is executable.
- App launches then returns immediately: inspect `/Roms/APPS/GarlicMP3/garlic-mp3.log`.
- UI opens but no sound: check that `mpg123` exists and can play an MP3 on the device.
- MP3 files are missing: use `/mnt/mmc/MUSIC` or the app-local `MUSIC` folder; only `.mp3` files are shown.
- Controls mismatch: press the button and inspect `garlic-mp3.log` for unknown joystick button IDs, then edit `src/input.c`.
- Volume does not change: ALSA mixer control names may differ on GarlicOS; test with `amixer`.
- Screen issues: the app requests a plain `640x480` SDL 1.2 software surface.

## Notes

This is a first-run skeleton. It avoids ID3 metadata, album art, playlists, config files, software mixing, and `libmpg123` linking until the app is confirmed working on RG35XX OG GarlicOS 1.4.9.
