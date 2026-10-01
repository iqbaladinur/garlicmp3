#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "audio_flac.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_WCHAR
#include "../third_party/dr_libs/dr_flac.h"

#ifndef F_SETPIPE_SZ
#define F_SETPIPE_SZ 1031
#endif

#define CHUNK_FRAMES 1024
#define PIPE_BYTES 32768           /* ~90 ms at 96 kHz stereo, ~190 ms at 44.1 kHz */
#define DEV_BUFFER "0.3"           /* out123 --devbuffer, seconds */
#define DEV_BUFFER_MS 300
#define OPEN_RETRIES 3             /* out123 failing to open the device at track start */
#define MAX_DRAINING 4

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static pthread_t thread;
static int thread_alive = 0;

/* Track / thread state, guarded by mu. */
static int active = 0;
static int paused = 0;
static int stop_req = 0;
static int decode_done = 0;
static int finished = 0;
static int finished_taken = 0;
static int failed = 0;
static unsigned long long frames_out = 0;
static unsigned long long start_frame = 0;
static unsigned long long total_frames = 0;
static int rate = 0;
static int channels = 0;
static int bits = 0;
static char last_error[192] = "";
static char cur_path[1024] = "";
static int open_retries = 0;
static int retrying = 0;

/* out123 child. Only touched from the main thread. */
static pid_t out_pid = -1;
static int out_fd = -1;
static int out_rate = 0;
static int out_channels = 0;
static pid_t draining[MAX_DRAINING];
static int draining_count = 0;

int flac_is_path(const char *path)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot && strcasecmp(dot, ".flac") == 0;
}

/* ---- out123 process ------------------------------------------------- */

static void reap_draining(void)
{
    int i = 0;
    while (i < draining_count) {
        int st;
        if (waitpid(draining[i], &st, WNOHANG) != 0) {
            draining[i] = draining[--draining_count];
        } else {
            i++;
        }
    }
}

static void close_out123(int graceful)
{
    if (out_fd >= 0) {
        close(out_fd);
        out_fd = -1;
    }
    if (out_pid > 0) {
        if (graceful && draining_count < MAX_DRAINING) {
            /* EOF on stdin: out123 plays what it has, then exits. */
            draining[draining_count++] = out_pid;
        } else {
            int st;
            kill(out_pid, SIGTERM);
            waitpid(out_pid, &st, 0);
        }
    }
    out_pid = -1;
    out_rate = out_channels = 0;
}

static int spawn_out123(int r, int ch)
{
    int data[2];
    int err[2];
    char rate_s[16];
    char ch_s[8];
    int child_errno = 0;
    ssize_t got;

    /* Before opening the device ourselves, make sure an earlier out123 has
     * released it (needed when ALSA has no software mixing). */
    while (draining_count > 0) {
        int st;
        waitpid(draining[--draining_count], &st, 0);
    }

    if (pipe(data) != 0) {
        snprintf(last_error, sizeof(last_error), "FLAC: pipe failed");
        return -1;
    }
    if (pipe(err) != 0) {
        close(data[0]);
        close(data[1]);
        snprintf(last_error, sizeof(last_error), "FLAC: pipe failed");
        return -1;
    }
    fcntl(err[1], F_SETFD, FD_CLOEXEC);
    snprintf(rate_s, sizeof(rate_s), "%d", r);
    snprintf(ch_s, sizeof(ch_s), "%d", ch);

    out_pid = fork();
    if (out_pid < 0) {
        close(data[0]); close(data[1]); close(err[0]); close(err[1]);
        out_pid = -1;
        snprintf(last_error, sizeof(last_error), "FLAC: fork failed");
        return -1;
    }
    if (out_pid == 0) {
        int fd;
        dup2(data[0], 0);
        for (fd = 3; fd < 256; fd++) {
            if (fd != err[1]) {
                close(fd);
            }
        }
        execlp("out123", "out123", "-r", rate_s, "-c", ch_s, "-e", "s16",
               "--devbuffer", DEV_BUFFER, (char *)0);
        child_errno = errno;
        if (write(err[1], &child_errno, sizeof(child_errno)) < 0) {
            /* nothing else we can do */
        }
        _exit(127);
    }

    close(data[0]);
    close(err[1]);
    got = read(err[0], &child_errno, sizeof(child_errno));
    close(err[0]);
    if (got > 0) {
        int st;
        waitpid(out_pid, &st, 0);
        out_pid = -1;
        close(data[1]);
        snprintf(last_error, sizeof(last_error), "FLAC: out123 not found (%s)", strerror(child_errno));
        return -1;
    }

    out_fd = data[1];
    fcntl(out_fd, F_SETFD, FD_CLOEXEC); /* never leak into mpg123 */
    fcntl(out_fd, F_SETPIPE_SZ, PIPE_BYTES);
    out_rate = r;
    out_channels = ch;
    printf("flac: out123 pid=%d rate=%d channels=%d\n", (int)out_pid, r, ch);
    return 0;
}

/* ---- decoder thread -------------------------------------------------- */

static uint32_t rng_state = 0x9e3779b9u;

static int32_t tpdf_noise(void)
{
    /* Two uniform values in [0, 65535] -> triangular in (-65536, 65536),
     * i.e. +/- 1 LSB at 16-bit, expressed in s32 (left-justified) units. */
    uint32_t a;
    uint32_t b;
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    a = rng_state & 0xffff;
    b = rng_state >> 16;
    return (int32_t)a - (int32_t)b;
}

static int write_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

typedef struct DecodeJob {
    drflac *flac;
    int fd;
} DecodeJob;

static void *decode_main(void *arg)
{
    DecodeJob job = *(DecodeJob *)arg;
    int16_t *pcm16;
    int32_t *pcm32 = NULL;
    int ch = (int)job.flac->channels;
    int deep = job.flac->bitsPerSample > 16;

    free(arg);
    pcm16 = (int16_t *)malloc(sizeof(int16_t) * CHUNK_FRAMES * (size_t)ch);
    if (deep) {
        pcm32 = (int32_t *)malloc(sizeof(int32_t) * CHUNK_FRAMES * (size_t)ch);
    }

    for (;;) {
        drflac_uint64 n;

        pthread_mutex_lock(&mu);
        while (paused && !stop_req) {
            pthread_cond_wait(&cv, &mu);
        }
        if (stop_req) {
            pthread_mutex_unlock(&mu);
            break;
        }
        pthread_mutex_unlock(&mu);

        if (!pcm16 || (deep && !pcm32)) {
            n = 0;
        } else if (deep) {
            drflac_uint64 i;
            n = drflac_read_pcm_frames_s32(job.flac, CHUNK_FRAMES, pcm32);
            for (i = 0; i < n * (drflac_uint64)ch; i++) {
                int64_t v = (int64_t)pcm32[i] + tpdf_noise() + 0x8000;
                v >>= 16;
                pcm16[i] = (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
            }
        } else {
            /* 16-bit (or less) sources: samples pass through bit-exact. */
            n = drflac_read_pcm_frames_s16(job.flac, CHUNK_FRAMES, pcm16);
        }

        if (n == 0) {
            pthread_mutex_lock(&mu);
            decode_done = 1;
            finished = 1;
            pthread_mutex_unlock(&mu);
            break;
        }
        if (write_all(job.fd, pcm16, (size_t)n * (size_t)ch * sizeof(int16_t)) != 0) {
            pthread_mutex_lock(&mu);
            if (!stop_req) {
                failed = 1;
                snprintf(last_error, sizeof(last_error), "FLAC: audio output stopped (device busy?)");
            }
            pthread_mutex_unlock(&mu);
            break;
        }
        pthread_mutex_lock(&mu);
        frames_out += n;
        pthread_mutex_unlock(&mu);
    }

    free(pcm16);
    free(pcm32);
    drflac_close(job.flac);
    return NULL;
}

static void join_thread(void)
{
    if (thread_alive) {
        pthread_join(thread, NULL);
        thread_alive = 0;
    }
}

/* ---- public API ------------------------------------------------------ */

void flac_stop(int graceful)
{
    int was_done;

    pthread_mutex_lock(&mu);
    stop_req = 1;
    was_done = decode_done;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);

    /* A thread blocked in write() is released by out123 going away. */
    if (!(graceful && was_done) && out_pid > 0) {
        kill(out_pid, SIGTERM);
    }
    join_thread();
    close_out123(graceful && was_done);

    pthread_mutex_lock(&mu);
    active = 0;
    paused = 0;
    decode_done = 0;
    finished = 0;
    finished_taken = 0;
    pthread_mutex_unlock(&mu);
}

int flac_play(const char *path, int start_seconds)
{
    drflac *f;
    DecodeJob *job;
    int reuse;

    signal(SIGPIPE, SIG_IGN);
    reap_draining();

    f = drflac_open_file(path, NULL);
    if (!f) {
        flac_stop(0);
        snprintf(last_error, sizeof(last_error), "FLAC: cannot open file");
        return -1;
    }
    if (f->channels < 1 || f->channels > 2) {
        drflac_close(f);
        flac_stop(0);
        snprintf(last_error, sizeof(last_error), "FLAC: %u channels not supported", (unsigned)f->channels);
        return -1;
    }

    /* Gapless: the previous track ended naturally and has the same format,
     * so keep feeding the running out123. Anything else restarts it. */
    pthread_mutex_lock(&mu);
    reuse = active && decode_done && !paused && !failed && out_pid > 0 &&
            out_rate == (int)f->sampleRate && out_channels == (int)f->channels;
    pthread_mutex_unlock(&mu);

    if (reuse) {
        join_thread();
    } else {
        flac_stop(0);
    }

    if (start_seconds > 0) {
        drflac_uint64 target = (drflac_uint64)start_seconds * f->sampleRate;
        if (target < f->totalPCMFrameCount && !drflac_seek_to_pcm_frame(f, target)) {
            target = 0;
            drflac_seek_to_pcm_frame(f, 0);
        }
        start_frame = target < f->totalPCMFrameCount ? target : 0;
    } else {
        start_frame = 0;
    }

    if (out_pid <= 0 && spawn_out123((int)f->sampleRate, (int)f->channels) != 0) {
        drflac_close(f);
        return -1;
    }

    job = (DecodeJob *)malloc(sizeof(*job));
    if (!job) {
        drflac_close(f);
        close_out123(0);
        snprintf(last_error, sizeof(last_error), "FLAC: out of memory");
        return -1;
    }
    job->flac = f;
    job->fd = out_fd;

    pthread_mutex_lock(&mu);
    active = 1;
    paused = 0;
    stop_req = 0;
    decode_done = 0;
    finished = 0;
    finished_taken = 0;
    failed = 0;
    frames_out = 0;
    total_frames = f->totalPCMFrameCount;
    rate = (int)f->sampleRate;
    channels = (int)f->channels;
    bits = (int)f->bitsPerSample;
    last_error[0] = '\0';
    pthread_mutex_unlock(&mu);

    snprintf(cur_path, sizeof(cur_path), "%s", path);
    if (!retrying) {
        open_retries = 0;
    }
    printf("flac: play %s %d-bit/%d Hz/%dch%s%s start=%d\n", path, bits, rate, channels,
           bits > 16 ? " (TPDF dither to 16-bit)" : " (bit-exact)", reuse ? " gapless" : "", start_seconds);

    if (pthread_create(&thread, NULL, decode_main, job) != 0) {
        free(job);
        drflac_close(f);
        close_out123(0);
        pthread_mutex_lock(&mu);
        active = 0;
        pthread_mutex_unlock(&mu);
        snprintf(last_error, sizeof(last_error), "FLAC: thread failed");
        return -1;
    }
    thread_alive = 1;
    return 0;
}

void flac_pause_toggle(void)
{
    pthread_mutex_lock(&mu);
    if (active) {
        paused = !paused;
        pthread_cond_broadcast(&cv);
    }
    pthread_mutex_unlock(&mu);
}

AudioState flac_state(void)
{
    AudioState s;
    pthread_mutex_lock(&mu);
    s = !active ? AUDIO_STOPPED : (paused ? AUDIO_PAUSED : AUDIO_PLAYING);
    pthread_mutex_unlock(&mu);
    return s;
}

void flac_poll(void)
{
    int stop_failed;
    int ended;

    reap_draining();

    pthread_mutex_lock(&mu);
    stop_failed = active && failed;
    /* Track ended, the finish was reported, and no next FLAC was started. */
    ended = active && decode_done && finished_taken;
    pthread_mutex_unlock(&mu);

    if (stop_failed) {
        char keep[sizeof(last_error)];
        unsigned long long early;
        int seconds;

        pthread_mutex_lock(&mu);
        early = frames_out;
        seconds = rate > 0 ? (int)(start_frame / (unsigned long long)rate) : 0;
        pthread_mutex_unlock(&mu);

        /* Output died before playing anything (device still held by
         * someone else for a moment): retry the same track a few times. */
        if (early <= (unsigned long long)(PIPE_BYTES / 2) && open_retries < OPEN_RETRIES && cur_path[0]) {
            char path[sizeof(cur_path)];
            open_retries++;
            snprintf(path, sizeof(path), "%s", cur_path);
            printf("flac: output not ready, retry %d/%d\n", open_retries, OPEN_RETRIES);
            flac_stop(0);
            usleep(200000 * (useconds_t)open_retries);
            retrying = 1;
            if (flac_play(path, seconds) == 0) {
                retrying = 0;
                return;
            }
            retrying = 0;
        }
        snprintf(keep, sizeof(keep), "%s", last_error);
        fprintf(stderr, "%s\n", keep);
        flac_stop(0);
        snprintf(last_error, sizeof(last_error), "%s", keep);
    } else if (ended) {
        flac_stop(1);
    }
}

int flac_elapsed_ms(void)
{
    unsigned long long heard;
    long long latency;
    int ms = 0;

    pthread_mutex_lock(&mu);
    if (active && rate > 0) {
        /* Audio still in the pipe and device buffer has not been heard. */
        latency = (long long)PIPE_BYTES / (channels * 2) + (long long)rate * DEV_BUFFER_MS / 1000;
        heard = frames_out > (unsigned long long)latency ? frames_out - (unsigned long long)latency : 0;
        if (paused || decode_done) {
            heard = frames_out;
        }
        ms = (int)((start_frame + heard) * 1000ULL / (unsigned long long)rate);
    }
    pthread_mutex_unlock(&mu);
    return ms;
}

int flac_duration_seconds(void)
{
    int s = 0;
    pthread_mutex_lock(&mu);
    if (active && rate > 0) {
        s = (int)((total_frames + (unsigned long long)rate / 2) / (unsigned long long)rate);
    }
    pthread_mutex_unlock(&mu);
    return s;
}

int flac_take_finished(void)
{
    int f;
    pthread_mutex_lock(&mu);
    f = finished;
    if (f) {
        finished = 0;
        finished_taken = 1;
    }
    pthread_mutex_unlock(&mu);
    return f;
}

const char *flac_last_error(void)
{
    return last_error;
}

void flac_shutdown(void)
{
    flac_stop(0);
    while (draining_count > 0) {
        int st;
        waitpid(draining[--draining_count], &st, 0);
    }
}
