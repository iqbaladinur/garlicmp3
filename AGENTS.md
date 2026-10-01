# AGENTS.md — GarlicMP3 development guide

Read this first. It is meant to let an agent start working without probing the
repo. `README.md` is the user-facing doc; `HANDOFF.md` is an **old** bring-up log
(input problems described there are solved) — treat it as history only.

## 1. What this is

A small C99 + SDL 1.2 music player (MP3 + FLAC) for the **original Anbernic
RG35XX on GarlicOS 1.4.9**. Not for RG35XX Plus/H/2024, MuOS, Knulli or H700.

Target facts that drive most decisions:

| Fact | Consequence |
|---|---|
| Actions ATM7029, **single-core** Cortex-A9, Linux 3.10, 256 MB | Background work must be niced/duty-cycled; UI and audio must win. |
| Screen 640x480, app uses a **16bpp (RGB565) SW surface** | `ui_sdl.c` writes `Uint16` pixels directly; `blend_px()` assumes 565. |
| Official toolchain: Miyoo **uClibc, possibly soft-float**, 32-bit | Hot loops are fixed-point; `long` is 32-bit (no epoch-ms in `long`); avoid `clock_gettime` (librt); link `-lpthread -lm` explicitly. |
| **ALSA has no software mixing** (exclusive device) | Only one process may hold the audio device; see §4.2. |
| Apps live on a FAT SD card; launcher `cd`s into the app dir | All runtime files are relative to the binary (`argv[0]` dir). |

## 2. Golden rules

1. **Two independent audio paths.** MP3 → `mpg123 -R` child (`audio_mpg123.c`).
   FLAC → dr_flac thread + `out123` child (`audio_flac.c`). `player.c` routes by
   extension. A FLAC failure must never break MP3 (and vice versa). Do not
   rewrite `audio_mpg123.c` to serve FLAC.
2. **Keep the FLAC path bit-exact** when EQ is Flat and RVA is off for 16-bit
   sources (`flac_dsp_is_bypass()`); everything else is TPDF-dithered to s16.
   If you touch `audio_flac.c`/`flac_dsp.c`, re-run the bit-exact check (§6.3).
3. **Never block the main loop** (it runs `SDL_Delay(33)` ≈ 30 fps). Heavy work
   goes to a thread (see `spectrum.c`) at reduced priority.
4. **No new runtime dependencies.** Third-party code is vendored single headers
   in `third_party/` (stb_image, minimp3, dr_flac, minimal SDL 1.2 headers).
5. **C99, `-Wall -Wextra` clean** with the device flags
   (`-std=c99 -Wall -Wextra -Os`). The only accepted warnings are
   `-Wformat-truncation` notes on bounded `snprintf` calls.
6. Pure-ASCII text uses the built-in 5x7 font; any non-ASCII string switches to
   16x16 "wide" mode with the CJK table. In `glyph5()` **bit 4 is the leftmost
   column**.

## 3. Repo map

```
APPS/GarlicMP3.sh        launcher: cd app dir, PATH=$PWD:..., SDL_AUDIODRIVER=alsa, logs to garlic-mp3.log
src/main.c               main loop, input dispatch, state/config/favorites/recent files, wiring
src/player.[ch]          router MP3 <-> FLAC (play/stop/pause/state/elapsed/finished/set_eq/set_rva)
src/audio_mpg123.[ch]    mpg123 -R child: LOAD/JUMP/PAUSE/STOP, @F/@P/@E parsing, EQ (SEQ), RVA, volume (sysfs)
src/audio_flac.[ch]      FLAC engine: decoder thread -> pipe -> out123; gapless reuse; open retries
src/flac_dsp.[ch]        fixed-point EQ shelves (689/1378 Hz), headroom, ReplayGain, TPDF dither
src/flac_meta.[ch]       FLAC STREAMINFO / Vorbis comments / ReplayGain / PICTURE (no decoding)
src/dr_flac_cfg.h        dr_flac options; include this, never dr_flac.h directly
src/dr_flac_impl.c       the one DR_FLAC_IMPLEMENTATION
src/spectrum.[ch]        background analyzer (minimp3 / dr_flac -> FFT -> 24 bands @25 fps) + disk cache
src/file_scan.[ch]       scan MUSIC dirs (1 subdir level), ID3/FLAC tags, duration, sort by folder+name
src/album_art.[ch]       cover: ID3 APIC / FLAC PICTURE / cover|folder|front.(jpg|png) -> RGBA (stb_image)
src/ui_sdl.[ch]          all drawing: list, now-playing (cover card, spectrum), settings, help
src/input.[ch]           SDL joystick (patched SDL) -> InputAction
src/settings.[ch]        settings screen model, EQ presets
src/font_cjk*.c          generated CJK glyph tables (tools/gen_cjk_font.py)
src/version.h            GARLICMP3_VERSION (bump manually); git hash injected by Makefile
tests/                   host unit tests (see §6.1)
tools/preview.c          renders UI screens to BMP with SDL dummy driver (real ui_sdl.c)
scripts/build-rg35xx-docker.sh  official device build (Docker)
scripts/preview-ui.sh, check-overlap.sh  UI preview / overlap check (podman + local image)
toolchain/               Dockerfile + SDL 1.2.15 RG35XX patch (vsync, hat joystick, no TIOCNOTTY)
third_party/             vendored headers (+ licenses)
docs/superpowers/        old design/plan notes for the mpg123 remote-mode EQ work
build_old_root_owned/    stale artifacts; ignore
```

## 4. Architecture

### 4.1 Main loop (`main.c`)
`player_poll()` → input (`input_poll_joystick`, `SDL_PollEvent`) → `dispatch_action`
→ `player_take_finished()` → auto-advance → `spectrum_set_active(playing path)`
→ render (`ui_render*`) → `SDL_Delay(33)`. State is saved every 5 s while
playing and on changes. Input calls `spectrum_note_input()` so background
analysis backs off.

### 4.2 Audio
- **MP3**: one persistent `mpg123 -R`. Position comes from `@F` lines (fractional
  seconds are kept in `audio_elapsed_ms`). EQ = `SEQ b m t` (bass = subband 0,
  mid = subband 1, treble = subbands 2..31), plus `VOLUME` headroom from
  `audio_headroom_volume_pct()`. Hardware volume is `/sys/class/volume/value`.
- **FLAC**: `flac_play()` opens dr_flac, spawns `out123 -r RATE -c CH -e s16
  --devbuffer 0.3` (stdin = pipe, 32 KB pipe), decoder thread writes s16.
  Same format + previous track ended naturally → reuse out123 (gapless).
  Exec failure is detected with a CLOEXEC error pipe. If out123 dies before
  consuming the first buffer it is retried 3x. Pause = decoder stops writing
  (ALSA underrun messages on pause are expected).
- **Device ownership** (hardware-verified bug): mpg123 keeps the ALSA device
  open after `STOP`. Therefore `player.c` calls `audio_shutdown()` (QUIT + wait)
  before every FLAC start; the next MP3 play respawns mpg123 via
  `ensure_player()` with stored settings. Going FLAC → MP3, `flac_stop(0)`
  kills and reaps out123 first.

### 4.3 Spectrum (`spectrum.c`)
Worker thread, `nice 10`. Priority: the active (playing) track at full speed
(eases off when 20 s ahead of the playhead); then the library one track at a
time, duty-cycled (~1/3 CPU) and paused 3 s after input. Output: 24 log bands
(45 Hz–16 kHz, +3 dB/oct tilt, −72..−30 dB → 0..255) at 25 fps, cached as
`cache/<fnv64(path|size|mtime)>.spc` ("GSPC" v1 header). Unreadable files get an
empty entry. UI samples it via `ui_set_spectrum_source()`; MP3 adds
`spectrum_latency_ms` (config), FLAC adds 0 (its elapsed time already
subtracts pipe + device buffer). No data yet → UI falls back to a synthesized
animation.

### 4.4 UI (`ui_sdl.c`)
Immediate-mode drawing every frame into the 565 surface. Now-playing order
matters: dot grid → **spectrum (background)** → cover halo/shadow → cover →
text → progress → info card. Cover art is decoded/scaled once per track
(`cover_sync`), masks are precomputed. Colors: shell `rgb(18,22,27)`, accent
`rgb(255,170,0)`; glow/spectrum tint comes from the cover's average color.

### 4.5 Runtime files (app dir)
`state.cfg` (last track/position/settings, wins over config), `config.cfg`
(startup defaults: `repeat_mode volume_step favorites_only debug eq_preset
eq_bass eq_mid eq_treble rva spectrum_background spectrum_latency_ms`),
`favorites.cfg`, `recent.cfg`, `cache/*.spc`, `garlic-mp3.log`.
Music dirs: `Roms/MUSIC` (SD2), `Roms/APPS/GarlicMP3/MUSIC`, `/mnt/mmc/MUSIC`.

## 5. Building

### 5.1 Official (needs Docker Hub access)
```sh
make docker-rg35xx-dist      # -> dist/APPS/{GarlicMP3.sh, GarlicMP3/{garlic-mp3-player,mpg123,out123,...}}
```
Builds `toolchain/` image (nfriedly/miyoo-toolchain:steward + patched SDL),
then copies static `mpg123` (and `out123` if present) from
`nfriedly/miyoo-toolchain:latest`.

### 5.2 Without Docker (e.g. cloud sandbox where Docker Hub is blocked)
Static ARM build with zig (`pip install ziglang`), verified to run on device:
```sh
Z=$(python3 -c "import ziglang,os;print(os.path.dirname(ziglang.__file__))")/zig
mkdir -p ~/zigbin
printf '#!/bin/sh\nexec %s cc -target arm-linux-musleabihf -mcpu=cortex_a9 "$@"\n' $Z > ~/zigbin/arm-cc
printf '#!/bin/sh\nexec %s ar "$@"\n' $Z > ~/zigbin/arm-ar
printf '#!/bin/sh\nexec %s ranlib "$@"\n' $Z > ~/zigbin/arm-ranlib
chmod +x ~/zigbin/*; export PATH=~/zigbin:$PATH

# SDL 1.2.15 + RG35XX patch (git clone works where libsdl.org is blocked)
git clone --depth 1 -b release-1.2.15 https://github.com/libsdl-org/SDL-1.2.git SDL-arm
cd SDL-arm && patch -p1 < ../garlicmp3/toolchain/build/patches/rg35xx-sdl-vsync.patch && ./autogen.sh
CC=arm-cc AR=arm-ar RANLIB=arm-ranlib ./configure --host=arm-linux-gnueabihf --prefix=$HOME/sdl-arm \
  --disable-shared --enable-static --disable-x11 --disable-video-opengl --disable-alsa \
  --disable-pulseaudio --disable-arts --disable-esd --disable-nasm --disable-video-directfb --disable-cdrom
make -j8 && make install && cd ..

# the app (note: --host must be gnueabihf for autoconf; zig still targets musl)
make clean && make dist CC=arm-cc NO_BUNDLED_LIBS=1 CFLAGS="-std=c99 -Wall -Wextra -Os" \
  LDFLAGS="-static -s" SDL_CFLAGS="-I$HOME/sdl-arm/include -I$HOME/sdl-arm/include/SDL -D_GNU_SOURCE=1 -D_REENTRANT" \
  SDL_LIBS="-L$HOME/sdl-arm/lib -lSDL"
```
`mpg123`/`out123` for this path: build mpg123 from `https://github.com/libsdl-org/mpg123.git`
static with the same zig wrappers against a static alsa-lib (v1.2.11,
`--prefix=/usr --disable-python --disable-ucm --disable-topology --disable-alisp`),
configured `--disable-modules --with-audio=alsa,oss --with-default-audio=alsa,oss
--with-cpu=arm_fpu`. Gotcha: after a fresh clone `touch` configure.ac/m4 →
aclocal.m4 → Makefile.in/config.h.in/configure in that order, or make tries to
re-run autotools and fails. Relink with `LDFLAGS="-static -s ..."` to strip
(zig's objcopy can't strip). Zip `dist/APPS/` and copy to `SD2:/Roms/APPS/`.

### 5.3 Host build (for running the real app on Linux)
Build SDL 1.2 statically for the host the same way (no `--host`), then:
```sh
make clean && make all CC=gcc CFLAGS="-std=c99 -Wall -Wextra -O2" \
  SDL_CFLAGS="-I$HOME/sdl-host/include" SDL_LIBS="-L$HOME/sdl-host/lib -lSDL -ldl"
```
`build/` is shared by host and ARM builds — always `make clean` when switching.

## 6. Testing

### 6.1 Unit tests (host)
```sh
make test-host   # stops at the first failure; run individually if needed:
cc -std=c99 -Wall -Wextra -Isrc -o build/test_audio    tests/test_audio.c src/audio_mpg123.c
cc -std=c99 -Wall -Wextra -Isrc -o build/test_spectrum tests/test_spectrum.c src/spectrum.c src/dr_flac_impl.c -lpthread -lm
cc -std=c99 -Wall -Wextra -Isrc -o build/test_flac     tests/test_flac.c src/flac_meta.c src/flac_dsp.c src/audio_mpg123.c -lm
```
**Pre-existing failures (not yours):** `test_settings` (`s.cursor ==
SETTINGS_ITEM_PRESET`) and `test_font` (missing U+2460 ①). Also compile every
file with the device flags: `for f in src/*.c; do cc -std=c99 -Wall -Wextra -Os
-c $f -Isrc -Ithird_party/sdl12-min -o /dev/null; done`.

### 6.2 UI screenshots
`tools/preview.c` renders list / now-playing / settings / help with the real
`ui_sdl.c` (SDL dummy driver). Without podman, compile it directly:
```sh
gcc -O2 -o /tmp/preview tools/preview.c src/settings.c src/ui_sdl.c src/album_art.c \
  src/flac_meta.c src/font_cjk.c src/font_cjk_tables.c -I$HOME/sdl-host/include -Isrc \
  -L$HOME/sdl-host/lib -lSDL -lm -lpthread -ldl
mkdir -p /out && cd /out && SDL_VIDEODRIVER=dummy /tmp/preview   # writes /out/*.bmp
```
(It expects `/out/test_cover.mp3`; see `scripts/preview-ui.sh` for the
generator.) README images live in `docs/screenshots/` (`assets/GarlicMP3.png` is
a copy of `now-playing.png`); regenerate them the same way with demo tracks
(cover art + a synthesized FLAC so the spectrum shows real data) and compile
with `-DGARLICMP3_GIT_HASH='"<hash>"'` so the footer is not `unknown`. To show the real spectrum, link `src/spectrum.c src/dr_flac_impl.c`,
call `spectrum_init/set_active` + `ui_set_spectrum_source(spectrum_sample, 0)`
and advance `ui_set_playback_ms()` across frames.

### 6.3 End-to-end on host (real app, real mpg123/out123)
- Build host mpg123 with `--with-audio=dummy`, and **patch
  `src/libout123/modules/dummy.c` `write_dummy()` to `usleep()` for the written
  duration** — otherwise "playback" finishes instantly and tracks skip.
- Put the binary, `mpg123`, `out123` in a dir with `MUSIC/`; start playback by
  writing `state.cfg` (`playing_path=./MUSIC/x.flac`, `resume_play=1`; paths
  must match the scan, i.e. `./MUSIC/...`). Run with `SDL_VIDEODRIVER=dummy
  timeout N ./garlic-mp3-player`. (GNU `timeout` signals the whole process
  group, so a final "FLAC: audio output stopped" at teardown is an artifact.)
- **Bit-exact check**: wrap `out123` with a script that `tee`s stdin to a file,
  then `cmp` against `ffmpeg -i x.flac -f s16le -acodec pcm_s16le ref.raw`.
- **Exclusive device simulation** (reproduces the hardware bug): wrappers for
  `mpg123` and `out123` that do `exec 9>devlock; flock -n 9 || exit 1; exec real "$@"`
  (the lock must be held by the audio process itself, not a parent).
- Test media: `ffmpeg -f lavfi -i "aevalsrc=..." -c:a flac -sample_fmt s16|s32`
  (add `-bits_per_raw_sample 24` for 24-bit), `-metadata REPLAYGAIN_TRACK_GAIN=...`,
  embedded cover via `-i cover.png -map 0:a -map 1:v -c:v png -disposition:v attached_pic`.

### 6.4 On device
Ask the user for `Roms/APPS/GarlicMP3/garlic-mp3.log`. Useful lines:
`audio_play:` (MP3), `flac: play ... (bit-exact|TPDF dither) [eq] [replaygain]
[gapless]`, `flac: output not ready, retry`, ALSA errors, `spectrum cache:`.
Set `debug=1` in `config.cfg` for `Action=` / heartbeat lines.

## 7. Conventions
- Small focused modules with a header comment explaining *why*; comments in
  English (some older UI comments are Indonesian — leave them).
- Pure logic goes in SDL-free files so it can be unit-tested on the host
  (`flac_meta`, `flac_dsp`, spectrum analyzer/cache, `settings`).
- Update `README.md` for user-visible behavior and config keys.
- Commit messages: imperative summary (`feat:`, `fix:`, `ui:` prefixes), body
  explains the why and how it was verified.

## 8. Known gaps / ideas
- FLAC: mono/stereo only; output always 16-bit (no hi-res passthrough yet).
- Visualizer levels ignore the EQ setting.
- Changing EQ mid-track on FLAC resets filter state (possible tiny click).
- Cover art is not shown in the track list, only in now-playing.
- `scripts/preview-ui.sh` / `check-overlap.sh` depend on podman and a local
  `aveferrum/rg35xx-toolchain` image.
