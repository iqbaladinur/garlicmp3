#define _POSIX_C_SOURCE 200809L
#include "audio_mpg123.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t player_pid = -1;
static AudioState state = AUDIO_STOPPED;
static time_t play_started_at = 0;
static time_t pause_started_at = 0;
static int paused_seconds = 0;
static int stopping = 0;
static int finished = 0;

#define SYS_VOLUME_MAX 40

static void reap_player(int blocking)
{
    int status = 0;
    int flags = blocking ? 0 : WNOHANG;

    if (player_pid <= 0) {
        state = AUDIO_STOPPED;
        return;
    }

    pid_t got = waitpid(player_pid, &status, flags);
    if (got == player_pid) {
        player_pid = -1;
        if (!stopping && state != AUDIO_STOPPED) {
            finished = 1;
        }
        state = AUDIO_STOPPED;
        play_started_at = 0;
        pause_started_at = 0;
        paused_seconds = 0;
    } else if (got < 0 && errno == ECHILD) {
        player_pid = -1;
        state = AUDIO_STOPPED;
        play_started_at = 0;
        pause_started_at = 0;
        paused_seconds = 0;
    }
}

void audio_poll(void)
{
    reap_player(0);
}

AudioState audio_state(void)
{
    audio_poll();
    return state;
}

void audio_stop(void)
{
    int i;

    if (player_pid <= 0) {
        state = AUDIO_STOPPED;
        play_started_at = 0;
        pause_started_at = 0;
        paused_seconds = 0;
        return;
    }

    stopping = 1;
    finished = 0;
    kill(player_pid, SIGTERM);
    for (i = 0; i < 20; i++) {
        reap_player(0);
        if (player_pid <= 0) {
            stopping = 0;
            return;
        }
        usleep(10000);
    }

    kill(player_pid, SIGKILL);
    for (i = 0; i < 20; i++) {
        reap_player(0);
        if (player_pid <= 0) {
            stopping = 0;
            return;
        }
        usleep(10000);
    }

    printf("audio_stop: failed to reap pid=%d\n", (int)player_pid);
    fflush(stdout);
    player_pid = -1;
    state = AUDIO_STOPPED;
    play_started_at = 0;
    pause_started_at = 0;
    paused_seconds = 0;
    stopping = 0;
}

int audio_play(const char *path)
{
    if (!path || !path[0]) {
        return -1;
    }

    audio_stop();

    player_pid = fork();
    if (player_pid < 0) {
        perror("fork");
        player_pid = -1;
        state = AUDIO_STOPPED;
        return -1;
    }

    if (player_pid == 0) {
        execlp("mpg123", "mpg123", "-q", path, (char *)0);
        perror("mpg123");
        _exit(127);
    }

    usleep(100000);
    reap_player(0);
    if (player_pid <= 0) {
        /* mpg123 exited within 100ms — failed to start */
        printf("audio_play: mpg123 exited immediately\n");
        fflush(stdout);
        return -1;
    }

    state = AUDIO_PLAYING;
    play_started_at = time(NULL);
    pause_started_at = 0;
    paused_seconds = 0;
    printf("audio_play: playing pid=%d\n", (int)player_pid);
    fflush(stdout);
    return 0;
}

void audio_pause_toggle(void)
{
    if (player_pid <= 0) {
        return;
    }

    if (state == AUDIO_PLAYING) {
        if (kill(player_pid, SIGSTOP) == 0) {
            state = AUDIO_PAUSED;
            pause_started_at = time(NULL);
        }
    } else if (state == AUDIO_PAUSED) {
        if (kill(player_pid, SIGCONT) == 0) {
            if (pause_started_at > 0) {
                paused_seconds += (int)(time(NULL) - pause_started_at);
            }
            pause_started_at = 0;
            state = AUDIO_PLAYING;
        }
    }
}

int audio_elapsed_seconds(void)
{
    time_t now;
    int elapsed;

    if (player_pid <= 0 || play_started_at <= 0 || state == AUDIO_STOPPED) {
        return 0;
    }

    now = state == AUDIO_PAUSED && pause_started_at > 0 ? pause_started_at : time(NULL);
    elapsed = (int)(now - play_started_at) - paused_seconds;
    return elapsed > 0 ? elapsed : 0;
}

int audio_take_finished(void)
{
    int was_finished;

    audio_poll();
    was_finished = finished;
    finished = 0;
    return was_finished;
}

static int read_sys_volume(void)
{
    FILE *fp = fopen("/sys/class/volume/value", "r");
    int value = 40;

    if (!fp) {
        perror("volume read");
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
        return;
    }

    fprintf(fp, "%d\n", value);
    fclose(fp);
    printf("volume=%d\n", value);
    fflush(stdout);
}

void audio_volume_down(void)
{
    write_sys_volume(read_sys_volume() - 5);
}

void audio_volume_up(void)
{
    write_sys_volume(read_sys_volume() + 5);
}

int audio_get_volume(void)
{
    return read_sys_volume();
}

void audio_set_volume(int value)
{
    write_sys_volume(value);
}
