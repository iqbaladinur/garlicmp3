#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
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
