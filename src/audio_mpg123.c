#define _POSIX_C_SOURCE 200809L
#include "audio_mpg123.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static pid_t player_pid = -1;
static AudioState state = AUDIO_STOPPED;

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
        state = AUDIO_STOPPED;
    } else if (got < 0 && errno == ECHILD) {
        player_pid = -1;
        state = AUDIO_STOPPED;
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
    if (player_pid <= 0) {
        state = AUDIO_STOPPED;
        return;
    }

    kill(player_pid, SIGTERM);
    reap_player(1);
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
    if (state == AUDIO_STOPPED) {
        return -1;
    }

    state = AUDIO_PLAYING;
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
        }
    } else if (state == AUDIO_PAUSED) {
        if (kill(player_pid, SIGCONT) == 0) {
            state = AUDIO_PLAYING;
        }
    }
}

static void run_amixer(const char *control, const char *delta)
{
    char command[128];
    int rc;

    snprintf(command, sizeof(command), "amixer set %s %s >/dev/null 2>&1", control, delta);
    rc = system(command);
    (void)rc;
}

void audio_volume_down(void)
{
    run_amixer("Master", "5%-");
    run_amixer("PCM", "5%-");
}

void audio_volume_up(void)
{
    run_amixer("Master", "5%+");
    run_amixer("PCM", "5%+");
}
