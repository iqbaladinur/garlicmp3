# Audio Remote-Mode Engine + Equalizer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the fork-per-track mpg123 subprocess with one persistent `mpg123 -R` remote-control process, adding gapless track changes, clean pause, accurate position/duration, a 3-band equalizer with presets, and ReplayGain normalization behind a separate Settings screen (hotkey R2).

**Architecture:** `src/audio_mpg123.c` is rewritten as a driver that spawns `mpg123 -R` once and talks to it over two pipes (commands on child stdin, `@`-prefixed events on child stdout, parsed in `audio_poll()`). A new pure-logic `src/settings.c` module holds the EQ/RVA menu state (values in tenths, `10` = 1.0 = flat). `main.c` gains a screen-state switch (library vs settings) and persists EQ state in `state.cfg`. All new pure logic is unit-tested on the host with plain `cc`; device build stays `make docker-rg35xx-dist`.

**Tech Stack:** C99, POSIX (fork/pipe/waitpid), SDL 1.2 (UI only), bundled static mpg123 (verified to support `-R`, `LOAD`, `LOADPAUSED`, `PAUSE`, `STOP`, `JUMP <n>s`, `VOLUME`, `SEQ`, `RVA`, `QUIT`).

**Spec:** `docs/superpowers/specs/2026-07-04-audio-remote-mode-eq-design.md`

## Global Constraints

- Branch: `feature/audio-remote-mode` (already created and checked out).
- Compiler flags: `-std=c99 -Wall -Wextra` — all code must compile warning-clean.
- No new external libraries. SDL 1.2 API only in UI/input; `audio_mpg123.c` and `settings.c` must stay SDL-free (they are compiled on the host for tests).
- The public API names in `src/audio_mpg123.h` used by `main.c` must keep working: `audio_play`, `audio_play_from_seconds`, `audio_stop`, `audio_pause_toggle`, `audio_state`, `audio_poll`, `audio_elapsed_seconds`, `audio_take_finished`, `audio_get_volume`, `audio_set_volume`, `audio_set_volume_step`, `audio_volume_down`, `audio_volume_up`, `audio_last_error`.
- Hardware volume stays `/sys/class/volume/value` (0..40). mpg123 software `VOLUME` is used ONLY for EQ anti-clipping headroom.
- EQ band values are integers in tenths: range 5..20 meaning 0.5..2.0; 10 = flat.
- The UI glyph font (`glyph5` in `ui_sdl.c`) only renders A-Z, 0-9, and `. - _ / : + ( ) &`. Do not use `<`, `>`, `%`, or other symbols in new UI strings.
- Host test commands: `make test-host` (must pass with no mpg123 installed), `make test-integration` (self-skips when mpg123 is absent; host has cc 11.4.0, no mpg123 as of writing).
- Commit messages end with: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

---

### Task 1: Settings model module (pure logic) + host test scaffolding

**Files:**
- Create: `src/settings.h`
- Create: `src/settings.c`
- Create: `tests/test_settings.c`
- Modify: `Makefile` (add `src/settings.c` to `SRCS`, add `test-host` target)

**Interfaces:**
- Consumes: nothing (pure module, no includes beyond libc).
- Produces (used by Tasks 5 and 6):
  - `typedef struct Settings { int cursor; int preset; int bass_t; int mid_t; int treble_t; int rva; } Settings;`
  - Item enum `SETTINGS_ITEM_PRESET, SETTINGS_ITEM_BASS, SETTINGS_ITEM_MID, SETTINGS_ITEM_TREBLE, SETTINGS_ITEM_RVA, SETTINGS_ITEM_COUNT`
  - Preset enum `EQ_PRESET_FLAT, EQ_PRESET_BASS_BOOST, EQ_PRESET_BASS_TREBLE, EQ_PRESET_VOCAL, EQ_PRESET_ROCK, EQ_PRESET_CUSTOM, EQ_PRESET_COUNT`
  - `void settings_init(Settings *s);`
  - `void settings_cursor_move(Settings *s, int direction);` (wraps)
  - `int settings_adjust(Settings *s, int direction);` (returns 1 when an audio-affecting value changed)
  - `void settings_set_preset(Settings *s, int preset);` (loads the preset's band triple; `EQ_PRESET_CUSTOM` keeps current bands)
  - `int settings_clamp_band(int value);`
  - `const char *settings_preset_name(int preset);`
  - `const char *settings_item_name(int item);`

- [ ] **Step 1: Write the failing test**

Create `tests/test_settings.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "settings.h"

static void test_init_defaults(void)
{
    Settings s;

    settings_init(&s);
    assert(s.cursor == SETTINGS_ITEM_PRESET);
    assert(s.preset == EQ_PRESET_FLAT);
    assert(s.bass_t == 10 && s.mid_t == 10 && s.treble_t == 10);
    assert(s.rva == 0);
}

static void test_cursor_wraps(void)
{
    Settings s;

    settings_init(&s);
    settings_cursor_move(&s, -1);
    assert(s.cursor == SETTINGS_ITEM_COUNT - 1);
    settings_cursor_move(&s, 1);
    assert(s.cursor == SETTINGS_ITEM_PRESET);
}

static void test_preset_cycle_loads_values(void)
{
    Settings s;

    settings_init(&s);
    s.cursor = SETTINGS_ITEM_PRESET;
    assert(settings_adjust(&s, 1) == 1);
    assert(s.preset == EQ_PRESET_BASS_BOOST);
    assert(s.bass_t == 16 && s.mid_t == 10 && s.treble_t == 10);
    /* cycling backwards from Flat wraps to Custom */
    settings_init(&s);
    s.cursor = SETTINGS_ITEM_PRESET;
    assert(settings_adjust(&s, -1) == 1);
    assert(s.preset == EQ_PRESET_CUSTOM);
}

static void test_custom_preset_keeps_values(void)
{
    Settings s;

    settings_init(&s);
    s.bass_t = 17;
    settings_set_preset(&s, EQ_PRESET_CUSTOM);
    assert(s.bass_t == 17);
    settings_set_preset(&s, EQ_PRESET_ROCK);
    assert(s.bass_t == 14 && s.mid_t == 9 && s.treble_t == 13);
}

static void test_manual_band_switches_to_custom(void)
{
    Settings s;

    settings_init(&s);
    s.cursor = SETTINGS_ITEM_BASS;
    assert(settings_adjust(&s, 1) == 1);
    assert(s.bass_t == 11);
    assert(s.preset == EQ_PRESET_CUSTOM);
}

static void test_band_clamps(void)
{
    Settings s;

    settings_init(&s);
    s.cursor = SETTINGS_ITEM_TREBLE;
    s.treble_t = EQ_BAND_MAX;
    assert(settings_adjust(&s, 1) == 0);
    assert(s.treble_t == EQ_BAND_MAX);
    s.treble_t = EQ_BAND_MIN;
    assert(settings_adjust(&s, -1) == 0);
    assert(s.treble_t == EQ_BAND_MIN);
    assert(settings_clamp_band(99) == EQ_BAND_MAX);
    assert(settings_clamp_band(-3) == EQ_BAND_MIN);
    assert(settings_clamp_band(12) == 12);
}

static void test_rva_toggle(void)
{
    Settings s;

    settings_init(&s);
    s.cursor = SETTINGS_ITEM_RVA;
    assert(settings_adjust(&s, 1) == 1);
    assert(s.rva == 1);
    assert(settings_adjust(&s, -1) == 1);
    assert(s.rva == 0);
}

static void test_names(void)
{
    assert(strcmp(settings_preset_name(EQ_PRESET_FLAT), "Flat") == 0);
    assert(strcmp(settings_preset_name(EQ_PRESET_CUSTOM), "Custom") == 0);
    assert(strcmp(settings_item_name(SETTINGS_ITEM_RVA), "Normalize (RVA)") == 0);
}

int main(void)
{
    test_init_defaults();
    test_cursor_wraps();
    test_preset_cycle_loads_values();
    test_custom_preset_keeps_values();
    test_manual_band_switches_to_custom();
    test_band_clamps();
    test_rva_toggle();
    test_names();
    printf("test_settings OK\n");
    return 0;
}
```

Add to `Makefile` (below the `docker-rg35xx-dist` target, above `clean`):

```make
HOST_CC ?= cc
HOST_CFLAGS ?= -std=c99 -Wall -Wextra -Isrc

.PHONY: test-host
test-host:
	@mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) -o build/test_settings tests/test_settings.c src/settings.c
	./build/test_settings
```

Also change the `SRCS` list to include the new module:

```make
SRCS := \
	src/main.c \
	src/audio_mpg123.c \
	src/file_scan.c \
	src/input.c \
	src/settings.c \
	src/ui_sdl.c
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test-host`
Expected: FAIL — `cc` errors with `settings.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `src/settings.h`:

```c
#ifndef SETTINGS_H
#define SETTINGS_H

enum {
    SETTINGS_ITEM_PRESET = 0,
    SETTINGS_ITEM_BASS,
    SETTINGS_ITEM_MID,
    SETTINGS_ITEM_TREBLE,
    SETTINGS_ITEM_RVA,
    SETTINGS_ITEM_COUNT
};

enum {
    EQ_PRESET_FLAT = 0,
    EQ_PRESET_BASS_BOOST,
    EQ_PRESET_BASS_TREBLE,
    EQ_PRESET_VOCAL,
    EQ_PRESET_ROCK,
    EQ_PRESET_CUSTOM,
    EQ_PRESET_COUNT
};

/* Band values are tenths of the mpg123 SEQ multiplier: 10 = 1.0 = flat. */
#define EQ_BAND_MIN 5
#define EQ_BAND_MAX 20

typedef struct Settings {
    int cursor;   /* SETTINGS_ITEM_* */
    int preset;   /* EQ_PRESET_* */
    int bass_t;
    int mid_t;
    int treble_t;
    int rva;      /* 0/1 */
} Settings;

void settings_init(Settings *s);
void settings_cursor_move(Settings *s, int direction);
int settings_adjust(Settings *s, int direction);
void settings_set_preset(Settings *s, int preset);
int settings_clamp_band(int value);
const char *settings_preset_name(int preset);
const char *settings_item_name(int item);

#endif
```

Create `src/settings.c`:

```c
#include "settings.h"

typedef struct EqPreset {
    const char *name;
    int bass_t;
    int mid_t;
    int treble_t;
} EqPreset;

static const EqPreset presets[EQ_PRESET_COUNT] = {
    { "Flat",        10, 10, 10 },
    { "Bass Boost",  16, 10, 10 },
    { "Bass+Treble", 15,  9, 14 },
    { "Vocal",        8, 14, 11 },
    { "Rock",        14,  9, 13 },
    { "Custom",      10, 10, 10 } /* band values unused: Custom keeps current */
};

void settings_init(Settings *s)
{
    s->cursor = SETTINGS_ITEM_PRESET;
    s->preset = EQ_PRESET_FLAT;
    s->bass_t = 10;
    s->mid_t = 10;
    s->treble_t = 10;
    s->rva = 0;
}

void settings_cursor_move(Settings *s, int direction)
{
    s->cursor += direction;
    if (s->cursor < 0) {
        s->cursor = SETTINGS_ITEM_COUNT - 1;
    } else if (s->cursor >= SETTINGS_ITEM_COUNT) {
        s->cursor = 0;
    }
}

int settings_clamp_band(int value)
{
    if (value < EQ_BAND_MIN) {
        return EQ_BAND_MIN;
    }
    if (value > EQ_BAND_MAX) {
        return EQ_BAND_MAX;
    }
    return value;
}

void settings_set_preset(Settings *s, int preset)
{
    if (preset < 0 || preset >= EQ_PRESET_COUNT) {
        preset = EQ_PRESET_FLAT;
    }
    s->preset = preset;
    if (preset != EQ_PRESET_CUSTOM) {
        s->bass_t = presets[preset].bass_t;
        s->mid_t = presets[preset].mid_t;
        s->treble_t = presets[preset].treble_t;
    }
}

static int adjust_band(Settings *s, int *band, int direction)
{
    int next = settings_clamp_band(*band + direction);

    if (next == *band) {
        return 0;
    }
    *band = next;
    s->preset = EQ_PRESET_CUSTOM;
    return 1;
}

int settings_adjust(Settings *s, int direction)
{
    switch (s->cursor) {
    case SETTINGS_ITEM_PRESET: {
        int next = s->preset + direction;
        if (next < 0) {
            next = EQ_PRESET_COUNT - 1;
        } else if (next >= EQ_PRESET_COUNT) {
            next = 0;
        }
        settings_set_preset(s, next);
        return 1;
    }
    case SETTINGS_ITEM_BASS:
        return adjust_band(s, &s->bass_t, direction);
    case SETTINGS_ITEM_MID:
        return adjust_band(s, &s->mid_t, direction);
    case SETTINGS_ITEM_TREBLE:
        return adjust_band(s, &s->treble_t, direction);
    case SETTINGS_ITEM_RVA:
        s->rva = !s->rva;
        return 1;
    default:
        return 0;
    }
}

const char *settings_preset_name(int preset)
{
    if (preset < 0 || preset >= EQ_PRESET_COUNT) {
        return "Flat";
    }
    return presets[preset].name;
}

const char *settings_item_name(int item)
{
    switch (item) {
    case SETTINGS_ITEM_PRESET:
        return "Preset";
    case SETTINGS_ITEM_BASS:
        return "Bass";
    case SETTINGS_ITEM_MID:
        return "Mid";
    case SETTINGS_ITEM_TREBLE:
        return "Treble";
    case SETTINGS_ITEM_RVA:
        return "Normalize (RVA)";
    default:
        return "";
    }
}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `make test-host`
Expected: prints `test_settings OK`, exit 0, no warnings.

- [ ] **Step 5: Commit**

```bash
git add src/settings.h src/settings.c tests/test_settings.c Makefile
git commit -m "feat: add EQ settings model with presets and host test scaffolding

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: Rewrite audio_mpg123.c as a persistent mpg123 -R remote driver

**Files:**
- Rewrite: `src/audio_mpg123.h`
- Rewrite: `src/audio_mpg123.c`
- Create: `tests/test_audio.c`
- Modify: `Makefile` (extend `test-host`)

**Interfaces:**
- Consumes: nothing new (POSIX only; must stay SDL-free).
- Produces (used by Tasks 3, 5, 6):
  - Preserved API listed in Global Constraints (same signatures as today).
  - New: `int audio_init(void);` (spawn `mpg123 -R`, wait for `@R` banner, returns 0/-1)
  - New: `void audio_shutdown(void);` (QUIT → SIGTERM → SIGKILL, reaps child)
  - New: `int audio_duration_seconds(void);`
  - New: `void audio_set_eq(int bass_tenths, int mid_tenths, int treble_tenths);`
  - New: `void audio_set_rva(int on);`
  - New: `int audio_headroom_volume_pct(int bass_tenths, int mid_tenths, int treble_tenths);` (pure)
  - Test hooks: `audio_test_reset`, `audio_test_begin_track`, `audio_test_handle_line`, `audio_test_get_state`, `audio_test_get_finished`, `audio_test_clear_finished`, `audio_test_get_elapsed`, `audio_test_get_duration`.

**Protocol notes (verified against the bundled binary's help strings):**
- Events: `@R` banner on start; `@F <frame> <frames-left> <sec> <sec-left>` during playback (floats); `@P 0|1|2` = stopped/paused/resumed; `@E ...` = error.
- Commands: `LOAD <path>`, `LOADPAUSED <path>`, `PAUSE` (toggle), `STOP`, `JUMP <n>s`, `VOLUME <pct>`, `SEQ <bass> <mid> <treble>`, `RVA off|mix`, `QUIT`.
- `LOAD` while another track plays may emit a `@P 0` for the replaced track; a `suppress_stop` flag (set by LOAD/STOP/@E, cleared by the next `@F` or one `@P 0`) prevents that from being mistaken for track-finished.
- `@E` while a track is loaded sets `finished=1` so `main.c`'s existing auto-advance skips bad tracks (replaces the old exited-within-100ms check).

- [ ] **Step 1: Write the failing test**

Create `tests/test_audio.c`:

```c
#include <assert.h>
#include <stdio.h>

#include "audio_mpg123.h"

static void test_frame_line_updates_position(void)
{
    audio_test_reset();
    audio_test_begin_track();
    audio_test_handle_line("@F 100 200 26.12 52.24");
    assert(audio_test_get_elapsed() == 26);
    assert(audio_test_get_duration() == 78); /* 26.12 + 52.24 = 78.36 -> 78 */
}

static void test_natural_finish(void)
{
    audio_test_reset();
    audio_test_begin_track();
    audio_test_handle_line("@F 100 200 26.12 52.24");
    audio_test_handle_line("@P 0");
    assert(audio_test_get_finished() == 1);
    assert(audio_test_get_state() == AUDIO_STOPPED);
}

static void test_load_swallows_replaced_track_stop(void)
{
    audio_test_reset();
    audio_test_begin_track(); /* sets suppress_stop like audio_play does */
    audio_test_handle_line("@P 0"); /* stop event from the replaced track */
    assert(audio_test_get_finished() == 0);
    assert(audio_test_get_state() == AUDIO_PLAYING);
    audio_test_handle_line("@F 1 500 0.02 90.00");
    audio_test_handle_line("@P 0"); /* real finish */
    assert(audio_test_get_finished() == 1);
}

static void test_pause_resume_events(void)
{
    audio_test_reset();
    audio_test_begin_track();
    audio_test_handle_line("@F 1 500 0.02 90.00");
    audio_test_handle_line("@P 1");
    assert(audio_test_get_state() == AUDIO_PAUSED);
    audio_test_handle_line("@P 2");
    assert(audio_test_get_state() == AUDIO_PLAYING);
}

static void test_error_marks_track_finished_once(void)
{
    audio_test_reset();
    audio_test_begin_track();
    audio_test_handle_line("@E Cannot open file");
    assert(audio_test_get_finished() == 1);
    assert(audio_test_get_state() == AUDIO_STOPPED);
    audio_test_clear_finished(); /* main consumed it via audio_take_finished */
    audio_test_handle_line("@P 0"); /* trailing stop after the error */
    assert(audio_test_get_finished() == 0); /* must not double-advance */
}

static void test_headroom(void)
{
    assert(audio_headroom_volume_pct(10, 10, 10) == 100);
    assert(audio_headroom_volume_pct(16, 10, 10) == 62);
    assert(audio_headroom_volume_pct(20, 20, 20) == 50);
    assert(audio_headroom_volume_pct(5, 8, 10) == 100);
}

int main(void)
{
    test_frame_line_updates_position();
    test_natural_finish();
    test_load_swallows_replaced_track_stop();
    test_pause_resume_events();
    test_error_marks_track_finished_once();
    test_headroom();
    printf("test_audio OK\n");
    return 0;
}
```

Extend the `test-host` target in `Makefile`:

```make
test-host:
	@mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) -o build/test_settings tests/test_settings.c src/settings.c
	./build/test_settings
	$(HOST_CC) $(HOST_CFLAGS) -o build/test_audio tests/test_audio.c src/audio_mpg123.c
	./build/test_audio
```

- [ ] **Step 2: Run test to verify it fails**

Run: `make test-host`
Expected: FAIL — undefined references to `audio_test_reset` etc. (old driver has no hooks).

- [ ] **Step 3: Write the implementation**

Replace `src/audio_mpg123.h` entirely:

```c
#ifndef AUDIO_MPG123_H
#define AUDIO_MPG123_H

typedef enum AudioState {
    AUDIO_STOPPED = 0,
    AUDIO_PLAYING,
    AUDIO_PAUSED
} AudioState;

int audio_init(void);
void audio_shutdown(void);
int audio_play(const char *path);
int audio_play_from_seconds(const char *path, int seconds);
void audio_stop(void);
void audio_pause_toggle(void);
void audio_volume_down(void);
void audio_volume_up(void);
int audio_get_volume(void);
void audio_set_volume(int value);
void audio_set_volume_step(int value);
void audio_set_eq(int bass_tenths, int mid_tenths, int treble_tenths);
void audio_set_rva(int on);
const char *audio_last_error(void);
AudioState audio_state(void);
void audio_poll(void);
int audio_elapsed_seconds(void);
int audio_duration_seconds(void);
int audio_take_finished(void);
int audio_headroom_volume_pct(int bass_tenths, int mid_tenths, int treble_tenths);

/* Test hooks: drive the event parser directly, no mpg123 process needed. */
void audio_test_reset(void);
void audio_test_begin_track(void);
void audio_test_handle_line(const char *line);
AudioState audio_test_get_state(void);
int audio_test_get_finished(void);
void audio_test_clear_finished(void);
int audio_test_get_elapsed(void);
int audio_test_get_duration(void);

#endif
```

Replace `src/audio_mpg123.c` entirely:

```c
#define _POSIX_C_SOURCE 200809L
#include "audio_mpg123.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SYS_VOLUME_MAX 40
#define EVENT_LINE_MAX 512
#define CMD_BUF_MAX 1152 /* "LOADPAUSED " + TRACK_PATH_MAX(1024) + slack */

static pid_t player_pid = -1;
static int cmd_fd = -1;   /* write end: mpg123 stdin */
static int event_fd = -1; /* read end: mpg123 stdout */
static int ready = 0;     /* saw @R banner */
static AudioState state = AUDIO_STOPPED;
static int suppress_stop = 0; /* swallow one @P 0 after LOAD/STOP/@E */
static int finished = 0;
static int elapsed_now = 0;
static int duration_now = 0;
static int volume_step = 5;
static char last_error[128] = "";
static char event_buf[EVENT_LINE_MAX];
static size_t event_len = 0;
/* Sound settings, replayed to mpg123 after every (re)spawn. Tenths: 10 = 1.0. */
static int eq_bass_t = 10;
static int eq_mid_t = 10;
static int eq_treble_t = 10;
static int rva_on = 0;

int audio_headroom_volume_pct(int bass_tenths, int mid_tenths, int treble_tenths)
{
    int max = bass_tenths;

    if (mid_tenths > max) {
        max = mid_tenths;
    }
    if (treble_tenths > max) {
        max = treble_tenths;
    }
    if (max <= 10) {
        return 100;
    }
    return 1000 / max;
}

static void close_pipes(void)
{
    if (cmd_fd >= 0) {
        close(cmd_fd);
        cmd_fd = -1;
    }
    if (event_fd >= 0) {
        close(event_fd);
        event_fd = -1;
    }
}

static void mark_dead(void)
{
    close_pipes();
    player_pid = -1;
    ready = 0;
    if (state != AUDIO_STOPPED) {
        /* Died mid-track: report as finished so main auto-advances,
         * which respawns the player on the next LOAD. */
        finished = 1;
        snprintf(last_error, sizeof(last_error), "mpg123 died, restarting");
    }
    state = AUDIO_STOPPED;
    suppress_stop = 0;
    elapsed_now = 0;
    duration_now = 0;
    event_len = 0;
}

static void reap_player(void)
{
    int status = 0;
    pid_t got;

    if (player_pid <= 0) {
        return;
    }
    got = waitpid(player_pid, &status, WNOHANG);
    if (got == player_pid || (got < 0 && errno == ECHILD)) {
        mark_dead();
    }
}

static void handle_line(const char *line)
{
    if (strncmp(line, "@R", 2) == 0) {
        ready = 1;
        return;
    }
    if (strncmp(line, "@F ", 3) == 0) {
        double sec = 0.0;
        double sec_left = 0.0;
        if (sscanf(line + 3, "%*d %*d %lf %lf", &sec, &sec_left) == 2) {
            elapsed_now = (int)sec;
            duration_now = (int)(sec + sec_left + 0.5);
        }
        suppress_stop = 0;
        return;
    }
    if (strncmp(line, "@P ", 3) == 0) {
        int p = atoi(line + 3);
        if (p == 0) {
            if (suppress_stop) {
                suppress_stop = 0;
                return;
            }
            if (state != AUDIO_STOPPED) {
                finished = 1;
            }
            state = AUDIO_STOPPED;
            elapsed_now = 0;
            duration_now = 0;
        } else if (p == 1) {
            state = AUDIO_PAUSED;
        } else if (p == 2) {
            state = AUDIO_PLAYING;
        }
        return;
    }
    if (strncmp(line, "@E", 2) == 0) {
        snprintf(last_error, sizeof(last_error), "mpg123:%.100s", line + 2);
        if (state != AUDIO_STOPPED) {
            finished = 1; /* auto-advance skips the bad track */
        }
        state = AUDIO_STOPPED;
        suppress_stop = 1; /* swallow the trailing @P 0, if any */
        elapsed_now = 0;
        duration_now = 0;
        return;
    }
    /* @S, @I, @V, @H, @J, @T etc. are ignored. */
}

static void drain_events(void)
{
    char chunk[256];
    ssize_t n;
    ssize_t i;

    if (event_fd < 0) {
        return;
    }

    for (;;) {
        n = read(event_fd, chunk, sizeof(chunk));
        if (n > 0) {
            for (i = 0; i < n; i++) {
                char c = chunk[i];
                if (c == '\n') {
                    event_buf[event_len] = '\0';
                    handle_line(event_buf);
                    event_len = 0;
                } else if (event_len < sizeof(event_buf) - 1) {
                    event_buf[event_len++] = c;
                }
            }
            continue;
        }
        if (n == 0) {
            mark_dead(); /* EOF: child is gone */
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        mark_dead();
        return;
    }
}

static int send_cmd(const char *fmt, ...)
{
    char buf[CMD_BUF_MAX];
    va_list ap;
    int len;

    if (cmd_fd < 0) {
        return -1;
    }
    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    if (len < 0) {
        return -1;
    }
    if (len > (int)sizeof(buf) - 2) {
        len = (int)sizeof(buf) - 2;
    }
    buf[len] = '\n';
    buf[len + 1] = '\0';
    if (write(cmd_fd, buf, (size_t)(len + 1)) < 0) {
        snprintf(last_error, sizeof(last_error), "mpg123 pipe write failed");
        mark_dead();
        return -1;
    }
    return 0;
}

static void apply_sound_settings(void)
{
    if (player_pid <= 0) {
        return;
    }
    send_cmd("VOLUME %d", audio_headroom_volume_pct(eq_bass_t, eq_mid_t, eq_treble_t));
    send_cmd("SEQ %d.%d %d.%d %d.%d",
             eq_bass_t / 10, eq_bass_t % 10,
             eq_mid_t / 10, eq_mid_t % 10,
             eq_treble_t / 10, eq_treble_t % 10);
    send_cmd("RVA %s", rva_on ? "mix" : "off");
}

static int spawn_player(void)
{
    int to_child[2];
    int from_child[2];
    int i;

    if (pipe(to_child) != 0) {
        snprintf(last_error, sizeof(last_error), "pipe failed");
        return -1;
    }
    if (pipe(from_child) != 0) {
        close(to_child[0]);
        close(to_child[1]);
        snprintf(last_error, sizeof(last_error), "pipe failed");
        return -1;
    }

    player_pid = fork();
    if (player_pid < 0) {
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        player_pid = -1;
        snprintf(last_error, sizeof(last_error), "Cannot fork mpg123");
        return -1;
    }

    if (player_pid == 0) {
        dup2(to_child[0], 0);
        dup2(from_child[1], 1);
        close(to_child[0]);
        close(to_child[1]);
        close(from_child[0]);
        close(from_child[1]);
        execlp("mpg123", "mpg123", "-R", (char *)0);
        _exit(127);
    }

    close(to_child[0]);
    close(from_child[1]);
    cmd_fd = to_child[1];
    event_fd = from_child[0];
    fcntl(event_fd, F_SETFL, fcntl(event_fd, F_GETFL, 0) | O_NONBLOCK);
    ready = 0;
    event_len = 0;
    state = AUDIO_STOPPED;
    suppress_stop = 0;
    elapsed_now = 0;
    duration_now = 0;

    for (i = 0; i < 100; i++) { /* up to 1s for the @R banner */
        drain_events();
        reap_player();
        if (ready) {
            break;
        }
        if (player_pid <= 0) {
            snprintf(last_error, sizeof(last_error), "mpg123 failed to start");
            return -1;
        }
        usleep(10000);
    }
    if (!ready) {
        kill(player_pid, SIGKILL);
        waitpid(player_pid, NULL, 0);
        player_pid = -1;
        close_pipes();
        snprintf(last_error, sizeof(last_error), "mpg123 not responding");
        return -1;
    }

    apply_sound_settings();
    printf("audio: mpg123 -R ready pid=%d\n", (int)player_pid);
    fflush(stdout);
    return 0;
}

static int ensure_player(void)
{
    reap_player();
    if (player_pid > 0) {
        return 0;
    }
    return spawn_player();
}

int audio_init(void)
{
    signal(SIGPIPE, SIG_IGN);
    return ensure_player();
}

void audio_shutdown(void)
{
    int i;

    if (player_pid <= 0) {
        close_pipes();
        return;
    }
    state = AUDIO_STOPPED; /* mark_dead must not set finished during teardown */
    suppress_stop = 1;
    send_cmd("QUIT");
    for (i = 0; i < 20; i++) {
        reap_player();
        if (player_pid <= 0) {
            return;
        }
        usleep(10000);
    }
    kill(player_pid, SIGTERM);
    for (i = 0; i < 20; i++) {
        reap_player();
        if (player_pid <= 0) {
            return;
        }
        usleep(10000);
    }
    kill(player_pid, SIGKILL);
    waitpid(player_pid, NULL, 0);
    player_pid = -1;
    close_pipes();
}

void audio_poll(void)
{
    drain_events();
    reap_player();
}

AudioState audio_state(void)
{
    audio_poll();
    return state;
}

static int audio_play_internal(const char *path, int start_seconds)
{
    if (!path || !path[0]) {
        snprintf(last_error, sizeof(last_error), "No audio path");
        return -1;
    }
    if (ensure_player() != 0) {
        return -1;
    }

    finished = 0;
    duration_now = 0;
    elapsed_now = start_seconds > 0 ? start_seconds : 0;
    suppress_stop = 1; /* swallow @P 0 from any replaced track */

    if (start_seconds > 1) {
        if (send_cmd("LOADPAUSED %s", path) != 0) {
            return -1;
        }
        send_cmd("JUMP %ds", start_seconds);
        send_cmd("PAUSE"); /* unpause */
    } else {
        if (send_cmd("LOAD %s", path) != 0) {
            return -1;
        }
    }

    state = AUDIO_PLAYING;
    last_error[0] = '\0';
    printf("audio_play: %s start=%d\n", path, start_seconds);
    fflush(stdout);
    return 0;
}

int audio_play(const char *path)
{
    return audio_play_internal(path, 0);
}

int audio_play_from_seconds(const char *path, int seconds)
{
    int rc;

    rc = audio_play_internal(path, seconds);
    if (rc != 0 && seconds > 0) {
        printf("audio_play: resume failed, retry from start\n");
        fflush(stdout);
        rc = audio_play_internal(path, 0);
    }
    return rc;
}

void audio_stop(void)
{
    reap_player();
    if (player_pid > 0 && state != AUDIO_STOPPED) {
        suppress_stop = 1;
        send_cmd("STOP");
    }
    state = AUDIO_STOPPED;
    finished = 0;
    elapsed_now = 0;
    duration_now = 0;
}

void audio_pause_toggle(void)
{
    audio_poll();
    if (player_pid <= 0 || state == AUDIO_STOPPED) {
        return;
    }
    if (send_cmd("PAUSE") == 0) {
        state = state == AUDIO_PLAYING ? AUDIO_PAUSED : AUDIO_PLAYING;
    }
}

int audio_elapsed_seconds(void)
{
    audio_poll();
    return elapsed_now;
}

int audio_duration_seconds(void)
{
    audio_poll();
    return duration_now;
}

int audio_take_finished(void)
{
    int was_finished;

    audio_poll();
    was_finished = finished;
    finished = 0;
    return was_finished;
}

void audio_set_eq(int bass_tenths, int mid_tenths, int treble_tenths)
{
    eq_bass_t = bass_tenths;
    eq_mid_t = mid_tenths;
    eq_treble_t = treble_tenths;
    apply_sound_settings();
}

void audio_set_rva(int on)
{
    rva_on = on ? 1 : 0;
    apply_sound_settings();
}

static int read_sys_volume(void)
{
    FILE *fp = fopen("/sys/class/volume/value", "r");
    int value = 40;

    if (!fp) {
        perror("volume read");
        snprintf(last_error, sizeof(last_error), "Cannot read system volume");
        return value;
    }

    if (fscanf(fp, "%d", &value) != 1) {
        value = 40;
    }
    fclose(fp);
    return value;
}

static void write_sys_volume(int value)
{
    FILE *fp;

    if (value < 0) {
        value = 0;
    }
    if (value > SYS_VOLUME_MAX) {
        value = SYS_VOLUME_MAX;
    }

    fp = fopen("/sys/class/volume/value", "w");
    if (!fp) {
        perror("volume write");
        snprintf(last_error, sizeof(last_error), "Cannot write system volume");
        return;
    }

    fprintf(fp, "%d\n", value);
    fclose(fp);
    last_error[0] = '\0';
    printf("volume=%d\n", value);
    fflush(stdout);
}

void audio_volume_down(void)
{
    write_sys_volume(read_sys_volume() - volume_step);
}

void audio_volume_up(void)
{
    write_sys_volume(read_sys_volume() + volume_step);
}

int audio_get_volume(void)
{
    return read_sys_volume();
}

void audio_set_volume(int value)
{
    write_sys_volume(value);
}

void audio_set_volume_step(int value)
{
    if (value < 1) {
        value = 1;
    }
    if (value > 20) {
        value = 20;
    }
    volume_step = value;
}

const char *audio_last_error(void)
{
    return last_error;
}

/* --- Test hooks ------------------------------------------------------- */

void audio_test_reset(void)
{
    player_pid = -1;
    cmd_fd = -1;
    event_fd = -1;
    ready = 0;
    state = AUDIO_STOPPED;
    suppress_stop = 0;
    finished = 0;
    elapsed_now = 0;
    duration_now = 0;
    event_len = 0;
    last_error[0] = '\0';
    eq_bass_t = 10;
    eq_mid_t = 10;
    eq_treble_t = 10;
    rva_on = 0;
}

void audio_test_begin_track(void)
{
    /* Mirrors the state changes audio_play_internal makes. */
    state = AUDIO_PLAYING;
    suppress_stop = 1;
    finished = 0;
    elapsed_now = 0;
    duration_now = 0;
}

void audio_test_handle_line(const char *line)
{
    handle_line(line);
}

AudioState audio_test_get_state(void)
{
    return state;
}

int audio_test_get_finished(void)
{
    return finished;
}

void audio_test_clear_finished(void)
{
    finished = 0;
}

int audio_test_get_elapsed(void)
{
    return elapsed_now;
}

int audio_test_get_duration(void)
{
    return duration_now;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `make test-host`
Expected: `test_settings OK` and `test_audio OK`, no warnings.

- [ ] **Step 5: Commit**

```bash
git add src/audio_mpg123.h src/audio_mpg123.c tests/test_audio.c Makefile
git commit -m "feat: rewrite audio driver on persistent mpg123 -R remote control

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: Host integration test against a real mpg123

**Files:**
- Create: `tests/test_audio_integration.c`
- Modify: `Makefile` (add `test-integration` target)

**Interfaces:**
- Consumes: the full driver API from Task 2 (`audio_init`, `audio_play`, `audio_take_finished`, `audio_set_eq`, `audio_set_rva`, `audio_pause_toggle`, `audio_stop`, `audio_duration_seconds`, `audio_shutdown`).
- Produces: nothing consumed by later tasks — a standalone verification binary.

The host currently has no `mpg123`, so the test must self-skip cleanly (`audio_init` fails → print SKIP, exit 0). With `mpg123` installed (`sudo apt install mpg123`) it verifies the spawn/error/respawn paths; with `TEST_MP3=/path/to/file.mp3` it additionally verifies real playback, duration, and pause.

- [ ] **Step 1: Write the test**

Create `tests/test_audio_integration.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "audio_mpg123.h"

static int wait_finished(int max_ticks)
{
    int i;

    for (i = 0; i < max_ticks; i++) {
        if (audio_take_finished()) {
            return 1;
        }
        usleep(10000);
    }
    return 0;
}

int main(void)
{
    const char *test_mp3;

    if (audio_init() != 0) {
        printf("SKIP: %s (install mpg123 to run this test)\n", audio_last_error());
        return 0;
    }

    /* A missing file must surface as a finished track (auto-skip path)
     * and must not kill the mpg123 process. */
    assert(audio_play("/nonexistent/garlic-test.mp3") == 0);
    assert(wait_finished(200) == 1);
    assert(audio_state() == AUDIO_STOPPED);

    /* EQ/RVA commands must be accepted; the process must survive them. */
    audio_set_eq(16, 10, 14);
    audio_set_rva(1);
    usleep(100000);
    audio_poll();
    assert(audio_play("/nonexistent/garlic-test2.mp3") == 0); /* pipe still alive */
    assert(wait_finished(200) == 1);

    test_mp3 = getenv("TEST_MP3");
    if (test_mp3 && test_mp3[0]) {
        assert(audio_play(test_mp3) == 0);
        sleep(1);
        assert(audio_state() == AUDIO_PLAYING);
        assert(audio_duration_seconds() > 0);
        assert(audio_elapsed_seconds() >= 0);
        audio_pause_toggle();
        usleep(300000);
        assert(audio_state() == AUDIO_PAUSED);
        audio_pause_toggle();
        usleep(300000);
        assert(audio_state() == AUDIO_PLAYING);
        audio_stop();
        usleep(300000);
        assert(audio_state() == AUDIO_STOPPED);
        assert(audio_take_finished() == 0); /* STOP is not a natural finish */
    } else {
        printf("note: set TEST_MP3=/path/to.mp3 for playback checks\n");
    }

    audio_shutdown();
    printf("test_audio_integration OK\n");
    return 0;
}
```

Add to `Makefile` after `test-host`:

```make
.PHONY: test-integration
test-integration:
	@mkdir -p build
	$(HOST_CC) $(HOST_CFLAGS) -o build/test_audio_integration tests/test_audio_integration.c src/audio_mpg123.c
	./build/test_audio_integration
```

- [ ] **Step 2: Run it (skips without mpg123)**

Run: `make test-integration`
Expected: compiles clean; prints `SKIP: mpg123 failed to start (install mpg123 to run this test)` (or the full pass if mpg123 is installed), exit 0.

- [ ] **Step 3: If possible, run the full path**

If allowed to install: `sudo apt-get install -y mpg123`, then `make test-integration` again.
Expected: `test_audio_integration OK`. If installation is not possible, note it and move on — on-device verification in Task 7 covers this.

- [ ] **Step 4: Commit**

```bash
git add tests/test_audio_integration.c Makefile
git commit -m "test: add mpg123 remote-mode integration test (self-skipping)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: Input — ACTION_SETTINGS_TOGGLE on R2 and SELECT+A

**Files:**
- Modify: `src/input.h` (add enum value)
- Modify: `src/input.c` (button mapping)

**Interfaces:**
- Consumes: nothing new.
- Produces (used by Task 6): `ACTION_SETTINGS_TOGGLE` member of `InputAction`.

R2's SDL button id on the RG35XX is not in the current enum (L2 is 6; ids 0-11 are taken). Map R2 as button 12 — **tentative, must be verified on device with `debug=1`** (unknown buttons print `JOY unknown btn=N`; adjust the constant if N differs). SELECT+A is the guaranteed fallback combo (SELECT combos already exist for Y and the hat; A alone stays PLAY).

- [ ] **Step 1: Add the action to `src/input.h`**

Change the enum (insert before `ACTION_QUIT`):

```c
    ACTION_VOL_DOWN,
    ACTION_VOL_UP,
    ACTION_SETTINGS_TOGGLE,
    ACTION_QUIT
```

- [ ] **Step 2: Map the buttons in `src/input.c`**

Add to the button-id enum (after `SDL_BTN_VOL_DOWN = 11`):

```c
    SDL_BTN_VOL_DOWN = 11,
    SDL_BTN_R2     = 12 /* tentative: verify with debug=1 on device */
```

In `button_action()`, change the `SDL_BTN_A` case and add `SDL_BTN_R2`:

```c
    case SDL_BTN_A:
        if (select_held) {
            select_combo_used = 1;
            return ACTION_SETTINGS_TOGGLE;
        }
        return ACTION_PLAY;
```

```c
    case SDL_BTN_R2:     return ACTION_SETTINGS_TOGGLE;
```

- [ ] **Step 3: Compile check**

Run: `cc -std=c99 -Wall -Wextra -Isrc -Ithird_party/sdl12-min -fsyntax-only src/input.c`
Expected: no output, exit 0.

- [ ] **Step 4: Commit**

```bash
git add src/input.h src/input.c
git commit -m "feat: add settings-toggle input action on R2 and Select+A

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 5: UI — settings screen renderer, real duration, EQ label

**Files:**
- Modify: `src/ui_sdl.h`
- Modify: `src/ui_sdl.c`

**Interfaces:**
- Consumes: `Settings`, `settings_item_name`, `settings_preset_name`, `SETTINGS_ITEM_*` from Task 1.
- Produces (used by Task 6):
  - `void ui_render(const TrackList *list, int selected, int playing, AudioState state, int elapsed_seconds, int duration_seconds, int volume, const char *repeat_label, const char *eq_label, int favorites_only, const char *message);` (two new params: `duration_seconds` after `elapsed_seconds`, `eq_label` after `repeat_label`)
  - `void ui_render_settings(const Settings *settings, AudioState state, const char *message);`

- [ ] **Step 1: Update `src/ui_sdl.h`**

Replace the declarations block:

```c
#ifndef UI_SDL_H
#define UI_SDL_H

#include "audio_mpg123.h"
#include "file_scan.h"
#include "settings.h"

#include <SDL/SDL.h>

int ui_init(void);
void ui_shutdown(void);
void ui_render(const TrackList *list, int selected, int playing, AudioState state, int elapsed_seconds, int duration_seconds, int volume, const char *repeat_label, const char *eq_label, int favorites_only, const char *message);
void ui_render_settings(const Settings *settings, AudioState state, const char *message);

#endif
```

- [ ] **Step 2: Update `ui_render` in `src/ui_sdl.c`**

Change the signature (line 469) to match the header above. Inside the body:

1. Replace the now-playing time/progress block (currently around lines 549-555):

```c
        {
            char time_label[32];
            int duration = duration_seconds > 0 ? duration_seconds :
                (playing >= 0 && playing < list->count ? list->tracks[playing].duration_seconds : 0);
            format_time_pair(elapsed_seconds, duration, time_label, sizeof(time_label));
            draw_text(454, 324, time_label, fg, 16);
            draw_progress_bar(454, 340, 120, 4, elapsed_seconds, duration, rgb(60, 70, 80), hi_text);
        }
```

2. After the repeat/favorites row (currently lines 513-516), draw the EQ label:

```c
    draw_text(38, 74, repeat_label ? repeat_label : "Repeat All", muted, 14);
    if (favorites_only) {
        draw_text(158, 74, "Favorites", hi, 12);
    }
    if (eq_label && eq_label[0]) {
        char eq_line[24];
        snprintf(eq_line, sizeof(eq_line), "EQ: %s", eq_label);
        draw_text(262, 74, eq_line, muted, 16);
    }
```

3. Update the idle hint string (currently line 594) to mention the hotkey:

```c
        draw_text(54, 405, "A Play B Stop X Pause Y Fav R2 Settings", muted, 47);
```

- [ ] **Step 3: Add `ui_render_settings` to `src/ui_sdl.c`**

Append at the end of the file:

```c
void ui_render_settings(const Settings *settings, AudioState state, const char *message)
{
    int i;
    Uint32 shell;
    Uint32 screen_bg;
    Uint32 border;
    Uint32 fg;
    Uint32 muted;
    Uint32 hi;
    Uint32 hi_text;
    Uint32 info_bg;
    Uint32 panel_shadow;

    if (!screen) {
        return;
    }

    shell = rgb(18, 22, 27);
    screen_bg = rgb(30, 36, 43);
    border = rgb(76, 88, 100);
    fg = rgb(235, 241, 246);
    muted = rgb(151, 163, 174);
    hi = rgb(33, 145, 226);
    hi_text = rgb(252, 254, 255);
    info_bg = rgb(24, 30, 36);
    panel_shadow = rgb(7, 10, 13);

    if (background) {
        SDL_BlitSurface(background, NULL, screen, NULL);
    }
    SDL_LockSurface(screen);
    if (!background) {
        draw_fallback_background();
    }

    fill_round_rect(18, 14, SCREEN_W - 36, SCREEN_H - 28, 18, panel_shadow);
    fill_round_rect(22, 18, SCREEN_W - 44, SCREEN_H - 36, 16, shell);
    draw_equalizer_bg(state);

    draw_text_scaled(38, 52, "Settings", fg, 10, 2);
    draw_text_right(594, 49, state_label(state), muted, 12);

    fill_round_rect(42, 108, 556, 252, 14, panel_shadow);
    fill_round_rect(38, 104, 556, 252, 14, border);
    fill_round_rect(40, 106, 552, 248, 12, screen_bg);

    for (i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        int y = 132 + i * 44;
        char value[24];
        Uint32 row_color = i == settings->cursor ? hi_text : fg;

        if (i == settings->cursor) {
            fill_round_rect(52, y - 10, 528, 32, 8, hi);
        }
        draw_text(72, y, settings_item_name(i), row_color, 24);

        switch (i) {
        case SETTINGS_ITEM_PRESET:
            snprintf(value, sizeof(value), "%s", settings_preset_name(settings->preset));
            break;
        case SETTINGS_ITEM_BASS:
            snprintf(value, sizeof(value), "%d.%d", settings->bass_t / 10, settings->bass_t % 10);
            break;
        case SETTINGS_ITEM_MID:
            snprintf(value, sizeof(value), "%d.%d", settings->mid_t / 10, settings->mid_t % 10);
            break;
        case SETTINGS_ITEM_TREBLE:
            snprintf(value, sizeof(value), "%d.%d", settings->treble_t / 10, settings->treble_t % 10);
            break;
        case SETTINGS_ITEM_RVA:
            snprintf(value, sizeof(value), "%s", settings->rva ? "On" : "Off");
            break;
        default:
            value[0] = '\0';
            break;
        }
        draw_text_right(560, y, value, row_color, 16);
    }

    fill_round_rect(42, 370, 556, 66, 13, panel_shadow);
    fill_round_rect(38, 366, 556, 66, 13, border);
    fill_round_rect(40, 368, 552, 62, 11, info_bg);
    if (message && message[0]) {
        draw_marquee_text(54, 385, message, hi, 47, 1);
    } else {
        draw_text(54, 385, "Up-Down select  Left-Right change", muted, 47);
    }
    draw_text(54, 405, "B or R2 to close", muted, 47);
    SDL_UnlockSurface(screen);
    SDL_Flip(screen);
}
```

(Note: only glyphs from the supported set are used — no `<`/`>` characters.)

- [ ] **Step 4: Compile check**

Run: `cc -std=c99 -Wall -Wextra -Isrc -Ithird_party/sdl12-min -fsyntax-only src/ui_sdl.c`
Expected: no output, exit 0. (`main.c` still calls the old `ui_render` signature — that is fixed in Task 6; do not compile `main.c` here.)

- [ ] **Step 5: Commit**

```bash
git add src/ui_sdl.h src/ui_sdl.c
git commit -m "feat: add settings screen renderer, real duration and EQ label in UI

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 6: main.c — screen routing, persistence, driver lifecycle

**Files:**
- Modify: `src/main.c`

**Interfaces:**
- Consumes: everything from Tasks 1, 2, 4, 5.
- Produces: the finished application flow; nothing downstream.

- [ ] **Step 1: Includes, types, and persistence fields**

Add `#include "settings.h"` after `#include "input.h"`.

Add after the `RepeatMode` enum:

```c
enum {
    SCREEN_LIBRARY = 0,
    SCREEN_SETTINGS
};
```

Extend `AppState` (fields appended):

```c
typedef struct AppState {
    char path[TRACK_PATH_MAX];
    char playing_path[TRACK_PATH_MAX];
    int volume;
    int resume_play;
    int elapsed_seconds;
    int repeat_mode;
    int debug;
    int favorites_only;
    int eq_preset;
    int eq_bass;
    int eq_mid;
    int eq_treble;
    int rva;
} AppState;
```

Extend `AppConfig`:

```c
typedef struct AppConfig {
    int repeat_mode;
    int debug;
    int favorites_only;
    int volume_step;
    int eq_preset;
    int eq_bass;
    int eq_mid;
    int eq_treble;
    int rva;
} AppConfig;
```

- [ ] **Step 2: Load/save the new keys**

In `load_config()`, add defaults after `config->volume_step = 5;`:

```c
    config->eq_preset = EQ_PRESET_FLAT;
    config->eq_bass = 10;
    config->eq_mid = 10;
    config->eq_treble = 10;
    config->rva = 0;
```

and parsing branches inside the `while` loop:

```c
        } else if (strncmp(line, "eq_preset=", 10) == 0) {
            config->eq_preset = atoi(line + 10);
            if (config->eq_preset < 0 || config->eq_preset >= EQ_PRESET_COUNT) {
                config->eq_preset = EQ_PRESET_FLAT;
            }
        } else if (strncmp(line, "eq_bass=", 8) == 0) {
            config->eq_bass = settings_clamp_band(atoi(line + 8));
        } else if (strncmp(line, "eq_mid=", 7) == 0) {
            config->eq_mid = settings_clamp_band(atoi(line + 7));
        } else if (strncmp(line, "eq_treble=", 10) == 0) {
            config->eq_treble = settings_clamp_band(atoi(line + 10));
        } else if (strncmp(line, "rva=", 4) == 0) {
            config->rva = atoi(line + 4) ? 1 : 0;
        }
```

In `load_state()`, initialize the new fields as unset after `state->favorites_only = -1;`:

```c
    state->eq_preset = -1;
    state->eq_bass = -1;
    state->eq_mid = -1;
    state->eq_treble = -1;
    state->rva = -1;
```

and add the same five parsing branches (using `state->` and, for `eq_preset`, resetting invalid values to `-1` instead of `EQ_PRESET_FLAT`):

```c
        } else if (strncmp(line, "eq_preset=", 10) == 0) {
            state->eq_preset = atoi(line + 10);
            if (state->eq_preset < 0 || state->eq_preset >= EQ_PRESET_COUNT) {
                state->eq_preset = -1;
            }
        } else if (strncmp(line, "eq_bass=", 8) == 0) {
            state->eq_bass = settings_clamp_band(atoi(line + 8));
        } else if (strncmp(line, "eq_mid=", 7) == 0) {
            state->eq_mid = settings_clamp_band(atoi(line + 7));
        } else if (strncmp(line, "eq_treble=", 10) == 0) {
            state->eq_treble = settings_clamp_band(atoi(line + 10));
        } else if (strncmp(line, "rva=", 4) == 0) {
            state->rva = atoi(line + 4) ? 1 : 0;
        }
```

Change `save_state()` signature to take the settings:

```c
static void save_state(const char *path, const TrackList *list, int selected, int playing, int repeat_mode, int debug, int favorites_only, const Settings *settings)
```

and write the new keys before `fclose(fp);`:

```c
    fprintf(fp, "eq_preset=%d\n", settings->preset);
    fprintf(fp, "eq_bass=%d\n", settings->bass_t);
    fprintf(fp, "eq_mid=%d\n", settings->mid_t);
    fprintf(fp, "eq_treble=%d\n", settings->treble_t);
    fprintf(fp, "rva=%d\n", settings->rva ? 1 : 0);
```

- [ ] **Step 3: Action routing for the settings screen**

Simplify the `ACTION_QUIT` case in `handle_action()` (the SIGSTOP-era pause-before-quit hack is obsolete; elapsed is now cached from `@F`):

```c
    case ACTION_QUIT:
        *running = 0;
        save_needed = 1;
        break;
```

Add above `main()`:

```c
static int handle_settings_action(InputAction action, Settings *settings, int *screen, char *message, size_t message_size)
{
    switch (action) {
    case ACTION_UP:
        settings_cursor_move(settings, -1);
        return 0;
    case ACTION_DOWN:
        settings_cursor_move(settings, 1);
        return 0;
    case ACTION_PREV:
    case ACTION_NEXT:
        if (settings_adjust(settings, action == ACTION_NEXT ? 1 : -1)) {
            audio_set_eq(settings->bass_t, settings->mid_t, settings->treble_t);
            audio_set_rva(settings->rva);
            snprintf(message, message_size, "EQ: %s", settings_preset_name(settings->preset));
            return 1;
        }
        return 0;
    case ACTION_STOP: /* B closes the menu */
        *screen = SCREEN_LIBRARY;
        return 0;
    case ACTION_PAUSE:
    case ACTION_VOL_DOWN:
    case ACTION_VOL_UP:
    case ACTION_QUIT:
        return -1; /* pass through to the library handler */
    default:
        return 0; /* consume everything else while the menu is open */
    }
}

static int dispatch_action(InputAction action, int *screen, Settings *settings, TrackList *list, int *selected, int *playing, int *repeat_mode, int *favorites_only, int *running, int debug, ShuffleHistory *shuffle_history, RecentList *recent, const char *favorites_path, const char *recent_path, char *message, size_t message_size)
{
    if (action == ACTION_SETTINGS_TOGGLE) {
        *screen = *screen == SCREEN_SETTINGS ? SCREEN_LIBRARY : SCREEN_SETTINGS;
        message[0] = '\0';
        return 0;
    }
    if (*screen == SCREEN_SETTINGS) {
        int handled = handle_settings_action(action, settings, screen, message, message_size);
        if (handled >= 0) {
            return handled;
        }
    }
    return handle_action(action, list, selected, playing, repeat_mode, favorites_only, running, debug, shuffle_history, recent, favorites_path, recent_path, message, message_size);
}
```

- [ ] **Step 4: Wire up `main()`**

Add locals near the top of `main()`:

```c
    int screen = SCREEN_LIBRARY;
    Settings settings;
```

After the `favorites_only = ...` resolution line (currently line 899), resolve settings state-over-config:

```c
    settings_init(&settings);
    settings_set_preset(&settings, saved_state.eq_preset >= 0 ? saved_state.eq_preset : config.eq_preset);
    if (settings.preset == EQ_PRESET_CUSTOM) {
        settings.bass_t = saved_state.eq_bass >= 0 ? saved_state.eq_bass : config.eq_bass;
        settings.mid_t = saved_state.eq_mid >= 0 ? saved_state.eq_mid : config.eq_mid;
        settings.treble_t = saved_state.eq_treble >= 0 ? saved_state.eq_treble : config.eq_treble;
    }
    settings.rva = saved_state.rva >= 0 ? saved_state.rva : config.rva;
```

After `printf("ui init done\n");`, start the driver and replay sound settings:

```c
    if (audio_init() != 0) {
        snprintf(message, sizeof(message), "%s", audio_last_error());
    }
    audio_set_eq(settings.bass_t, settings.mid_t, settings.treble_t);
    audio_set_rva(settings.rva);
```

Replace both dispatch call sites in the loop (joystick poll and SDL event) with:

```c
            if (dispatch_action(action, &screen, &settings, &list, &selected, &playing, &repeat_mode, &favorites_only, &running, debug, &shuffle_history, &recent, favorites_path, recent_path, message, sizeof(message))) {
                save_state(state_path, &list, selected, playing, repeat_mode, debug, favorites_only, &settings);
                last_state_save = SDL_GetTicks();
            }
```

Add `, &settings` to the two remaining `save_state(...)` calls inside the loop (auto-advance and the 5-second periodic save) and to the final one before shutdown.

Replace the render call (currently line 1001) with:

```c
        if (screen == SCREEN_SETTINGS) {
            ui_render_settings(&settings, audio_state(), message);
        } else {
            ui_render(&list, selected, playing, audio_state(), audio_elapsed_seconds(), audio_duration_seconds(), audio_get_volume(), repeat_label(repeat_mode), settings_preset_name(settings.preset), favorites_only, message);
        }
```

Replace the final `audio_stop();` (currently line 1006, after the last `save_state`) with:

```c
    audio_shutdown();
```

- [ ] **Step 5: Compile check + host tests**

Run:
```
cc -std=c99 -Wall -Wextra -Isrc -Ithird_party/sdl12-min -fsyntax-only src/main.c src/ui_sdl.c src/input.c src/settings.c src/audio_mpg123.c src/file_scan.c
make test-host
```
Expected: syntax check silent; `test_settings OK` and `test_audio OK`.

- [ ] **Step 6: Commit**

```bash
git add src/main.c
git commit -m "feat: wire settings screen, EQ persistence and remote audio lifecycle

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 7: Device build, docs, and on-device verification

**Files:**
- Modify: `README.md` (features list)
- Build output: `dist/APPS/`

- [ ] **Step 1: Device build**

Run: `make docker-rg35xx-dist` (requires Docker; first run may build the toolchain image and take a while).
Expected: `dist/APPS/GarlicMP3/garlic-mp3-player` rebuilt (ARM ELF), `mpg123` copied, no compiler warnings in the log.

- [ ] **Step 2: Update README.md**

In the features list, replace:

```markdown
- MP3 playback through a bundled static `mpg123` subprocess.
```

with:

```markdown
- MP3 playback through a single persistent `mpg123 -R` remote-control process: gapless track changes, clean pause/resume, accurate elapsed/duration (VBR included) and precise resume.
- 3-band equalizer (bass/mid/treble) with presets (Flat, Bass Boost, Bass+Treble, Vocal, Rock, Custom), ReplayGain (`RVA mix`) normalization toggle, and automatic anti-clipping headroom.
- Separate Settings screen on the R2 button (or Select+A): Up/Down selects, Left/Right adjusts, B closes; changes apply live and persist in `state.cfg`.
```

Also extend the `state.cfg` resume bullet to mention EQ:

```markdown
- Resume state saved in `state.cfg`, including selected track, active track, elapsed time, repeat mode, favorites-only mode, debug flag, volume, equalizer preset/bands, and RVA mode.
```

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: describe remote-mode engine, equalizer and settings screen

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

- [ ] **Step 4: On-device verification checklist (manual, requires hardware)**

Copy `dist/APPS/` to the SD card and verify on the RG35XX:

1. App starts; log shows `audio: mpg123 -R ready pid=...`.
2. Play a track; progress bar advances and shows `mm:ss / mm:ss` with a real total.
3. Track change (Next) has no audible gap or click; end-of-track auto-advance is seamless.
4. Pause (X) and resume produce no pop.
5. R2 opens Settings (if not, set `debug=1` in `config.cfg`, press R2, read `JOY unknown btn=N` from the log, fix `SDL_BTN_R2` in `src/input.c`, rebuild; Select+A works meanwhile).
6. Cycling presets audibly changes the sound while playing; Bass Boost does not clip at max hardware volume (headroom check).
7. Manual band edit switches preset display to Custom.
8. RVA On normalizes loudness across differently-mastered tracks.
9. Quit and relaunch: playback resumes at the saved position (accurate, VBR file included), EQ/RVA settings restored.
10. `ps` on device shows no zombie mpg123 processes after quit.
11. Tune the preset triples by ear if needed (values in `src/settings.c`).

---

## Self-Review Notes

- Spec coverage: gapless LOAD (Task 2), clean PAUSE (Task 2), accurate position/duration + JUMP resume (Tasks 2, 5, 6), SEQ EQ + presets + Custom (Tasks 1, 2, 6), RVA toggle (Tasks 1, 2, 6), headroom VOLUME (Task 2), separate Settings screen on R2 (Tasks 4, 5, 6), persistence in state.cfg/config.cfg (Task 6), respawn on death (Task 2), QUIT teardown (Task 2/6), host tests (Tasks 1-3), on-device checks (Task 7). Out-of-scope items (seek FF/RW, formats, crossfade) intentionally absent.
- Known tentative point: R2 = SDL button 12 (verified only on device; Select+A fallback guaranteed).
- Type consistency: `Settings` struct fields (`bass_t`, `mid_t`, `treble_t`, `rva`, `preset`, `cursor`) and `audio_set_eq(int,int,int)`/`audio_set_rva(int)` used identically across Tasks 1, 2, 5, 6.
