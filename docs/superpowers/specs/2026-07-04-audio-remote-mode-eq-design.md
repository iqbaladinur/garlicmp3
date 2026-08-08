# Audio Remote-Mode Engine + Equalizer Design

**Date:** 2026-07-04
**Branch:** `feature/audio-remote-mode`
**Goal:** Improve music playing quality on the RG35XX GarlicOS MP3 player: smooth
playback (gapless track changes, clean pause/resume) and sound quality (3-band
equalizer with presets, ReplayGain normalization), plus accurate position/duration
as a byproduct.

## Background

Playback today forks a fresh `mpg123` subprocess per track (`src/audio_mpg123.c`).
This causes:

- An audible gap on every track change (fork/exec + audio device reopen + 100ms
  startup sleep).
- Pause via `SIGSTOP`, which freezes the process mid-buffer and can pop on resume.
- Elapsed time approximated from wall-clock `time()`; no track duration at all.
- Resume via `mpg123 -k` with a hardcoded 38 frames/sec guess — wrong for VBR.
- No equalizer, no volume normalization.

The bundled static `mpg123` binary (from the Miyoo buildroot toolchain, copied by
`scripts/build-rg35xx-docker.sh`) was verified to support everything needed:
`-R` generic remote interface, `LOAD`, `LOADPAUSED`, `PAUSE`, `JUMP`, `VOLUME`,
`SEQ` (3-band eq), `EQ` (32-band), `RVA`, `SCAN`, `SILENCE`, plus gapless decoding.
No mpg123 rebuild is required.

## Architecture

Rewrite `src/audio_mpg123.c` as a driver for **one persistent `mpg123 -R`
subprocess**, spawned once at app start:

- Commands are written to the child's stdin (pipe).
- Events (`@F`, `@P`, `@E`, `@S`, ...) are read from the child's stdout
  (non-blocking pipe), parsed inside `audio_poll()`.

The existing public API in `audio_mpg123.h` is preserved (`audio_play`,
`audio_play_from_seconds`, `audio_stop`, `audio_pause_toggle`, `audio_state`,
`audio_elapsed_seconds`, `audio_take_finished`, `audio_poll`, volume functions,
`audio_last_error`), so `main.c` is mostly untouched for basic playback. New API
is added for EQ, RVA, and duration.

Other modules (`file_scan`, `input`, `ui_sdl`) change only where new features
require it (new input actions, settings screen rendering).

## Playback smoothness

- **Track change** = send `LOAD <path>` to the running process. No fork, no
  audio-device reopen; mpg123's gapless decoding stays active. The inter-track
  gap effectively disappears.
- **Pause/resume** = the `PAUSE` remote command (a clean decoder pause), replacing
  `SIGSTOP`/`SIGCONT`. No pop on resume.
- **Resume at position** (app startup restore) = `LOADPAUSED <path>` +
  `JUMP <seconds>s` + `PAUSE` (unpause). Sample-accurate, replacing the
  `-k 38*seconds` frame-skip guess.

## Position and duration

`audio_poll()` parses `@F <frame> <frames-left> <sec> <sec-left>` lines:

- Elapsed = `<sec>`; duration = `<sec> + <sec-left>`. Accurate for VBR.
- Wall-clock timing (`play_started_at`, `paused_seconds`) is removed.
- Track completion is detected from `@P 0` after a track was playing (feeds
  `audio_take_finished()` as today).

New API: `int audio_duration_seconds(void)`.

UI: the progress bar uses real duration; the time display shows
`mm:ss / mm:ss`.

## Sound quality and equalizer

- **3-band EQ** via `SEQ <bass> <mid> <treble>` (linear multipliers, 1.0 = flat).
  Applies in real time to the playing track — no restart needed.
- **Presets** (bass/mid/treble triples; starting values, tunable by ear on
  device):
  - Flat: 1.0 / 1.0 / 1.0
  - Bass Boost: 1.6 / 1.0 / 1.0
  - Bass+Treble: 1.5 / 0.9 / 1.4
  - Vocal: 0.8 / 1.4 / 1.1
  - Rock: 1.4 / 0.9 / 1.3
  - Custom: whatever the user last set manually

  Manually editing a band switches the preset to Custom.
- **Band range**: 0.5–2.0, step 0.1.
- **Normalization**: `RVA mix` (ReplayGain) toggle, Off by default (preserves
  current behavior).
- **Anti-clipping headroom**: when any band is boosted above 1.0, software
  `VOLUME` is reduced proportionally to the highest band
  (`volume_pct = 100 / max_band`), so boosts do not clip. The main volume
  buttons keep controlling hardware volume via `/sys/class/volume` exactly as
  today; software VOLUME is used only for headroom.

New API (names indicative): `audio_set_eq(double bass, double mid, double treble)`,
`audio_set_rva(int on)`.

## Settings screen and input

- **Hotkey `R2`** (currently unmapped) toggles a **separate Settings screen** —
  not an overlay. `B` also closes it. Playback continues while the menu is open;
  changes are audible immediately.
- Inside Settings: **up/down** selects an item, **left/right** changes its value.
  Items:
  1. `Preset` — Flat / Bass Boost / Bass+Treble / Vocal / Rock / Custom
  2. `Bass` — 0.5–2.0
  3. `Mid` — 0.5–2.0
  4. `Treble` — 0.5–2.0
  5. `Normalization (RVA)` — Off / On
- `main.c` gains a screen-state enum (`SCREEN_LIBRARY` / `SCREEN_SETTINGS`);
  while in Settings, navigation actions are routed to the menu instead of the
  track list.
- New input actions: `ACTION_SETTINGS_TOGGLE` (R2), plus reuse of
  UP/DOWN/PREV/NEXT for menu navigation.
- `ui_sdl` gains `ui_render_settings(...)`. The main screen's status line shows
  the active preset name next to the repeat/shuffle indicators. There is no
  preset-cycle button on the main screen; all EQ control lives in the menu.

## Persistence

- `state.cfg` gains: `eq_preset`, `eq_bass`, `eq_mid`, `eq_treble`, `rva`.
- `config.cfg` may provide startup defaults for the same keys.
- On startup (and after a respawn), the saved EQ/RVA/volume settings are replayed
  to the mpg123 process before playback resumes.

## Error handling

- **Unexpected mpg123 death** (detected via `waitpid(WNOHANG)` or pipe EOF/EPIPE):
  mark the driver dead, auto-respawn on the next command, replay EQ/RVA/volume
  state, surface a short message via `audio_last_error`.
- **Spawn failure at startup**: report via `audio_last_error` as today; the UI
  keeps running without audio.
- **Pipe write failure**: same dead-driver → respawn path.
- App exit sends `QUIT` to mpg123 and falls back to SIGTERM/SIGKILL + reap,
  keeping the current no-zombie guarantee.

## Testing

- The mpg123 remote protocol is identical on desktop Linux, so the driver is
  testable on the host with a stock `mpg123` binary via a small test harness
  (spawn, LOAD a fixture MP3, assert `@F` parsing, PAUSE/JUMP/SEQ behavior,
  kill-and-respawn path).
- Final verification on device: gapless track changes, pop-free pause/resume,
  audible EQ changes from the Settings screen, accurate progress bar and resume
  position, settings persisted across restarts.

## Out of scope

- Additional formats (FLAC/OGG), crossfade, 32-band graphic EQ UI, seeking
  (FF/RW) within a track — the `JUMP` plumbing makes seeking easy to add later,
  but it is not part of this work.
