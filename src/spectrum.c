#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "spectrum.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "../third_party/minimp3/minimp3.h"
#include "dr_flac_cfg.h"
#include <strings.h>

/* ------------------------------------------------------------------------
 * Analyzer: Hann-windowed 1024-point FFT every hop, power summed into
 * log-spaced bands (45 Hz .. 16 kHz), converted to dB with a mild
 * +3 dB/octave tilt (music falls off with frequency), mapped to 0..255.
 * ---------------------------------------------------------------------- */

#define FFT_N 1024
#define FFT_LOG2 10
#define BAND_LO_HZ 45.0f
#define BAND_HI_HZ 16000.0f
#define DB_FLOOR (-72.0f)
#define DB_RANGE 42.0f

struct SpectrumAnalyzer {
    int sample_rate;
    int hop;
    int since_hop;
    int filled;
    int pos;
    float ring[FFT_N];
    float window[FFT_N];
    float re[FFT_N];
    float im[FFT_N];
    float cos_t[FFT_N / 2];
    float sin_t[FFT_N / 2];
    unsigned short bitrev[FFT_N];
    int band_lo[SPECTRUM_BANDS];
    int band_hi[SPECTRUM_BANDS];
    float band_tilt_db[SPECTRUM_BANDS];
};

SpectrumAnalyzer *spectrum_analyzer_new(int sample_rate)
{
    SpectrumAnalyzer *a;
    int i;
    float bin_hz;
    int prev_hi = 1;

    if (sample_rate <= 0) {
        return NULL;
    }
    a = (SpectrumAnalyzer *)calloc(1, sizeof(*a));
    if (!a) {
        return NULL;
    }
    a->sample_rate = sample_rate;
    a->hop = sample_rate / SPECTRUM_FPS;
    if (a->hop < 1) {
        a->hop = 1;
    }
    for (i = 0; i < FFT_N; i++) {
        unsigned r = 0;
        int b;
        a->window[i] = 0.5f - 0.5f * cosf(2.0f * 3.14159265f * (float)i / (float)(FFT_N - 1));
        for (b = 0; b < FFT_LOG2; b++) {
            if (i & (1 << b)) {
                r |= 1u << (FFT_LOG2 - 1 - b);
            }
        }
        a->bitrev[i] = (unsigned short)r;
    }
    for (i = 0; i < FFT_N / 2; i++) {
        a->cos_t[i] = cosf(-2.0f * 3.14159265f * (float)i / (float)FFT_N);
        a->sin_t[i] = sinf(-2.0f * 3.14159265f * (float)i / (float)FFT_N);
    }

    /* Band edges in FFT bins; every band gets at least one bin of its own. */
    bin_hz = (float)sample_rate / (float)FFT_N;
    for (i = 0; i < SPECTRUM_BANDS; i++) {
        float f_hi = BAND_LO_HZ * powf(BAND_HI_HZ / BAND_LO_HZ, (float)(i + 1) / (float)SPECTRUM_BANDS);
        float f_mid = BAND_LO_HZ * powf(BAND_HI_HZ / BAND_LO_HZ, ((float)i + 0.5f) / (float)SPECTRUM_BANDS);
        int hi = (int)(f_hi / bin_hz + 0.5f);
        if (hi <= prev_hi) {
            hi = prev_hi + 1;
        }
        if (hi > FFT_N / 2) {
            hi = FFT_N / 2;
        }
        a->band_lo[i] = prev_hi < FFT_N / 2 ? prev_hi : FFT_N / 2 - 1;
        a->band_hi[i] = hi > a->band_lo[i] ? hi : a->band_lo[i] + 1;
        prev_hi = a->band_hi[i];
        a->band_tilt_db[i] = 3.0f * log2f(f_mid / 1000.0f);
    }
    return a;
}

void spectrum_analyzer_free(SpectrumAnalyzer *a)
{
    free(a);
}

static void fft(SpectrumAnalyzer *a)
{
    int size;

    for (size = 2; size <= FFT_N; size <<= 1) {
        int half = size >> 1;
        int step = FFT_N / size;
        int start;
        for (start = 0; start < FFT_N; start += size) {
            int k;
            for (k = 0; k < half; k++) {
                float wr = a->cos_t[k * step];
                float wi = a->sin_t[k * step];
                int i0 = start + k;
                int i1 = i0 + half;
                float tr = a->re[i1] * wr - a->im[i1] * wi;
                float ti = a->re[i1] * wi + a->im[i1] * wr;
                a->re[i1] = a->re[i0] - tr;
                a->im[i1] = a->im[i0] - ti;
                a->re[i0] += tr;
                a->im[i0] += ti;
            }
        }
    }
}

static void analyze_window(SpectrumAnalyzer *a, unsigned char *row)
{
    int i;
    /* Full-scale sine through a Hann window peaks at |X| = N/4. */
    const float ref_power = (float)(FFT_N / 4) * (float)(FFT_N / 4);

    for (i = 0; i < FFT_N; i++) {
        int src = (a->pos + i) % FFT_N; /* oldest sample first */
        int dst = a->bitrev[i];
        a->re[dst] = a->ring[src] * a->window[i];
        a->im[dst] = 0.0f;
    }
    fft(a);

    for (i = 0; i < SPECTRUM_BANDS; i++) {
        float p = 0.0f;
        float db;
        float v;
        int k;
        for (k = a->band_lo[i]; k < a->band_hi[i]; k++) {
            p += a->re[k] * a->re[k] + a->im[k] * a->im[k];
        }
        p /= (float)(a->band_hi[i] - a->band_lo[i]);
        db = 10.0f * log10f(p / ref_power + 1e-12f) + a->band_tilt_db[i];
        v = (db - DB_FLOOR) / DB_RANGE;
        if (v < 0.0f) {
            v = 0.0f;
        }
        if (v > 1.0f) {
            v = 1.0f;
        }
        row[i] = (unsigned char)(v * 255.0f + 0.5f);
    }
}

int spectrum_analyzer_feed(SpectrumAnalyzer *a, const short *pcm, int frames, int channels,
                           unsigned char *out, int out_rows)
{
    int n;
    int rows = 0;

    if (!a || !pcm || channels < 1) {
        return 0;
    }
    for (n = 0; n < frames; n++) {
        float s = 0.0f;
        int c;
        for (c = 0; c < channels; c++) {
            s += (float)pcm[n * channels + c];
        }
        a->ring[a->pos] = s / (32768.0f * (float)channels);
        a->pos = (a->pos + 1) % FFT_N;
        if (a->filled < FFT_N) {
            a->filled++;
        }
        if (++a->since_hop >= a->hop) {
            a->since_hop = 0;
            if (rows < out_rows) {
                analyze_window(a, out + rows * SPECTRUM_BANDS);
                rows++;
            }
        }
    }
    return rows;
}

/* ------------------------------------------------------------------------
 * File decode: stream the MP3 through minimp3 in 16 KB chunks.
 * ---------------------------------------------------------------------- */

typedef int (*RowSink)(void *ctx, const unsigned char *rows, int count);

static int analyze_mp3_stream(const char *path, RowSink sink, void *ctx)
{
    FILE *fp;
    static const int BUF = 16384;
    unsigned char *buf;
    short *pcm;
    unsigned char rows[8 * SPECTRUM_BANDS];
    mp3dec_t dec;
    SpectrumAnalyzer *an = NULL;
    int have = 0;
    int eof = 0;
    int total = 0;
    int rc = 0;

    fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    buf = (unsigned char *)malloc(BUF);
    pcm = (short *)malloc(sizeof(short) * MINIMP3_MAX_SAMPLES_PER_FRAME);
    if (!buf || !pcm) {
        free(buf);
        free(pcm);
        fclose(fp);
        return -1;
    }
    mp3dec_init(&dec);

    for (;;) {
        mp3dec_frame_info_t info;
        int samples;

        if (!eof && have < BUF) {
            size_t got = fread(buf + have, 1, (size_t)(BUF - have), fp);
            if (got == 0) {
                eof = 1;
            }
            have += (int)got;
        }
        if (have == 0) {
            break;
        }
        samples = mp3dec_decode_frame(&dec, buf, have, pcm, &info);
        if (info.frame_bytes == 0) {
            /* Need more data (or garbage at the end). */
            if (eof) {
                break;
            }
            if (have >= BUF) {
                have = 0; /* unsyncable chunk; drop it */
            }
            continue;
        }
        memmove(buf, buf + info.frame_bytes, (size_t)(have - info.frame_bytes));
        have -= info.frame_bytes;
        if (samples <= 0) {
            continue;
        }
        if (!an) {
            an = spectrum_analyzer_new(info.hz);
            if (!an) {
                rc = -1;
                break;
            }
        }
        {
            int n = spectrum_analyzer_feed(an, pcm, samples, info.channels, rows, 8);
            total += n;
            if (sink && sink(ctx, rows, n)) {
                rc = -1;
                break;
            }
        }
    }

    spectrum_analyzer_free(an);
    free(buf);
    free(pcm);
    fclose(fp);
    return rc < 0 ? -1 : total;
}

/* FLAC: same analyzer, fed from dr_flac (s16). */
static int analyze_flac_stream(const char *path, RowSink sink, void *ctx)
{
    drflac *f = drflac_open_file(path, NULL);
    SpectrumAnalyzer *an;
    short *pcm;
    unsigned char rows[8 * SPECTRUM_BANDS];
    int total = 0;
    int rc = 0;

    if (!f) {
        return -1;
    }
    an = spectrum_analyzer_new((int)f->sampleRate);
    pcm = (short *)malloc(sizeof(short) * 1152 * (size_t)(f->channels ? f->channels : 1));
    if (!an || !pcm || f->channels < 1) {
        spectrum_analyzer_free(an);
        free(pcm);
        drflac_close(f);
        return -1;
    }
    for (;;) {
        drflac_uint64 got = drflac_read_pcm_frames_s16(f, 1152, pcm);
        int n;
        if (got == 0) {
            break;
        }
        n = spectrum_analyzer_feed(an, pcm, (int)got, (int)f->channels, rows, 8);
        total += n;
        if (sink && sink(ctx, rows, n)) {
            rc = -1;
            break;
        }
    }
    spectrum_analyzer_free(an);
    free(pcm);
    drflac_close(f);
    return rc < 0 ? -1 : total;
}

static int analyze_stream(const char *path, RowSink sink, void *ctx)
{
    const char *dot = strrchr(path, '.');
    if (dot && strcasecmp(dot, ".flac") == 0) {
        return analyze_flac_stream(path, sink, ctx);
    }
    return analyze_mp3_stream(path, sink, ctx);
}

typedef struct CollectCtx {
    unsigned char *data;
    int count;
    int cap;
    int (*abort_fn)(void *ctx, int rows_done);
    void *abort_ctx;
} CollectCtx;

static int collect_rows(void *vctx, const unsigned char *rows, int count)
{
    CollectCtx *c = (CollectCtx *)vctx;

    if (c->count + count > c->cap) {
        int cap = c->cap ? c->cap * 2 : 4096;
        unsigned char *p;
        while (cap < c->count + count) {
            cap *= 2;
        }
        p = (unsigned char *)realloc(c->data, (size_t)cap * SPECTRUM_BANDS);
        if (!p) {
            return 1;
        }
        c->data = p;
        c->cap = cap;
    }
    memcpy(c->data + (size_t)c->count * SPECTRUM_BANDS, rows, (size_t)count * SPECTRUM_BANDS);
    c->count += count;
    return c->abort_fn ? c->abort_fn(c->abort_ctx, c->count) : 0;
}

int spectrum_analyze_file(const char *path, unsigned char **out_rows,
                          int (*abort_fn)(void *ctx, int rows_done), void *ctx)
{
    CollectCtx c;
    int rc;

    memset(&c, 0, sizeof(c));
    c.abort_fn = abort_fn;
    c.abort_ctx = ctx;
    rc = analyze_stream(path, collect_rows, &c);
    if (rc < 0) {
        free(c.data);
        *out_rows = NULL;
        return -1;
    }
    *out_rows = c.data;
    return c.count;
}

/* ------------------------------------------------------------------------
 * Cache files: "GSPC" magic, version, bands, fps, row count, then rows.
 * ---------------------------------------------------------------------- */

#define CACHE_VERSION 1

static void put_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
    p[2] = (unsigned char)((v >> 16) & 0xff);
    p[3] = (unsigned char)((v >> 24) & 0xff);
}

static uint32_t get_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int spectrum_cache_name(const char *cache_dir, const char *track_path, char *out, size_t out_size)
{
    struct stat st;
    uint64_t h = 1469598103934665603ULL;
    const unsigned char *p;
    char extra[64];

    if (!cache_dir || !track_path || stat(track_path, &st) != 0) {
        return -1;
    }
    snprintf(extra, sizeof(extra), "|%ld|%ld", (long)st.st_size, (long)st.st_mtime);
    for (p = (const unsigned char *)track_path; *p; p++) {
        h = (h ^ *p) * 1099511628211ULL;
    }
    for (p = (const unsigned char *)extra; *p; p++) {
        h = (h ^ *p) * 1099511628211ULL;
    }
    snprintf(out, out_size, "%s/%08lx%08lx.spc", cache_dir,
             (unsigned long)(h >> 32), (unsigned long)(h & 0xffffffffUL));
    return 0;
}

int spectrum_cache_write(const char *cache_file, const unsigned char *rows, int count)
{
    char tmp[1100];
    unsigned char hdr[16];
    FILE *fp;
    int ok;

    snprintf(tmp, sizeof(tmp), "%s.tmp", cache_file);
    fp = fopen(tmp, "wb");
    if (!fp) {
        return -1;
    }
    memcpy(hdr, "GSPC", 4);
    hdr[4] = CACHE_VERSION;
    hdr[5] = SPECTRUM_BANDS;
    hdr[6] = SPECTRUM_FPS;
    hdr[7] = 0;
    put_u32(hdr + 8, (uint32_t)(count > 0 ? count : 0));
    put_u32(hdr + 12, 0);
    ok = fwrite(hdr, 1, sizeof(hdr), fp) == sizeof(hdr);
    if (ok && count > 0) {
        ok = fwrite(rows, SPECTRUM_BANDS, (size_t)count, fp) == (size_t)count;
    }
    if (fclose(fp) != 0) {
        ok = 0;
    }
    if (!ok || rename(tmp, cache_file) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

int spectrum_cache_read(const char *cache_file, unsigned char **rows)
{
    unsigned char hdr[16];
    FILE *fp;
    uint32_t count;

    *rows = NULL;
    fp = fopen(cache_file, "rb");
    if (!fp) {
        return -1;
    }
    if (fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr) || memcmp(hdr, "GSPC", 4) != 0 ||
        hdr[4] != CACHE_VERSION || hdr[5] != SPECTRUM_BANDS || hdr[6] != SPECTRUM_FPS) {
        fclose(fp);
        return -1;
    }
    count = get_u32(hdr + 8);
    if (count > 25u * 60u * 60u * 4u) { /* > 4 hours: corrupt */
        fclose(fp);
        return -1;
    }
    if (count > 0) {
        *rows = (unsigned char *)malloc((size_t)count * SPECTRUM_BANDS);
        if (!*rows || fread(*rows, SPECTRUM_BANDS, count, fp) != count) {
            free(*rows);
            *rows = NULL;
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    return (int)count;
}

/* ------------------------------------------------------------------------
 * Worker thread.
 * ---------------------------------------------------------------------- */

#define ACTIVE_LEAD_MS 20000    /* active job eases off once this far ahead */
#define INPUT_BACKOFF_MS 3000   /* background pauses after a button press */
#define IDLE_DUTY_SLEEP_X 2     /* background sleeps 2x its busy time */

static pthread_t worker;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static int started = 0;
static int quitting = 0;
static char cache_root[1024];
static int background_on = 1;

static char **lib_paths = NULL;
static int lib_count = 0;
static int lib_next = 0;
static unsigned lib_gen = 0;

/* Active track state (guarded by mu). */
static char active_path[1024];
static unsigned active_gen = 0;
static unsigned char *active_rows = NULL;
static int active_count = 0;
static int active_cap = 0;
static int active_complete = 0;
static int active_requested_ms = 0;
static long last_input_ms = -100000;

/* gettimeofday rather than clock_gettime: older uClibc toolchains keep the
 * latter in librt, and wall-clock precision is plenty for back-off timing. */
static long now_ms(void)
{
    /* Relative to the first call: epoch milliseconds overflow a 32-bit long. */
    static time_t base_sec = 0;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (base_sec == 0) {
        base_sec = tv.tv_sec;
    }
    return (long)(tv.tv_sec - base_sec) * 1000L + (long)(tv.tv_usec / 1000);
}

static void sleep_ms(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static void set_thread_nice(int value)
{
#ifdef SYS_gettid
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), value);
#else
    (void)value;
#endif
}

typedef struct ActiveJob {
    unsigned gen;
} ActiveJob;

static int active_sink(void *vctx, const unsigned char *rows, int count)
{
    ActiveJob *job = (ActiveJob *)vctx;
    int abort_now = 0;
    int ahead_ms = 0;

    pthread_mutex_lock(&mu);
    if (quitting || job->gen != active_gen) {
        abort_now = 1;
    } else if (count > 0) {
        if (active_count + count > active_cap) {
            int cap = active_cap ? active_cap * 2 : 4096;
            unsigned char *p;
            while (cap < active_count + count) {
                cap *= 2;
            }
            p = (unsigned char *)realloc(active_rows, (size_t)cap * SPECTRUM_BANDS);
            if (!p) {
                abort_now = 1;
            } else {
                active_rows = p;
                active_cap = cap;
            }
        }
        if (!abort_now) {
            memcpy(active_rows + (size_t)active_count * SPECTRUM_BANDS, rows, (size_t)count * SPECTRUM_BANDS);
            active_count += count;
            ahead_ms = active_count * (1000 / SPECTRUM_FPS) - active_requested_ms;
        }
    }
    pthread_mutex_unlock(&mu);

    /* Far enough ahead of the playhead: yield a little per MP3 frame so the
     * rest finishes at several times real time without hogging the CPU
     * (it still completes long before the track ends, so the cache is
     * written even if the user skips later). */
    if (!abort_now && ahead_ms > ACTIVE_LEAD_MS) {
        sleep_ms(1);
    }
    return abort_now;
}

typedef struct IdleJob {
    unsigned lib_gen;
    long busy_since;
    int aborted;
} IdleJob;

static int idle_should_stop_locked(const IdleJob *job)
{
    return quitting || !background_on || job->lib_gen != lib_gen ||
           (active_path[0] && !active_complete);
}

static int idle_abort(void *vctx, int rows_done)
{
    IdleJob *job = (IdleJob *)vctx;
    int stop;

    (void)rows_done;
    pthread_mutex_lock(&mu);
    stop = idle_should_stop_locked(job);
    pthread_mutex_unlock(&mu);
    if (stop) {
        job->aborted = 1;
        return 1;
    }

    /* Duty-cycle: after ~100 ms of work, sleep twice that. Also back off
     * entirely while the user is actively pressing buttons. */
    {
        long now = now_ms();
        long busy = now - job->busy_since;
        if (busy >= 100) {
            sleep_ms((int)(busy * IDLE_DUTY_SLEEP_X));
            job->busy_since = now_ms();
        }
        for (;;) {
            long since;
            pthread_mutex_lock(&mu);
            since = now_ms() - last_input_ms;
            stop = idle_should_stop_locked(job);
            pthread_mutex_unlock(&mu);
            if (stop) {
                job->aborted = 1;
                return 1;
            }
            if (since >= INPUT_BACKOFF_MS) {
                break;
            }
            sleep_ms(200);
            job->busy_since = now_ms();
        }
    }
    return 0;
}

static void run_active_job(const char *path, unsigned gen)
{
    char cache_file[1100];
    unsigned char *rows = NULL;
    int count;
    int have_name = spectrum_cache_name(cache_root, path, cache_file, sizeof(cache_file)) == 0;

    if (have_name && (count = spectrum_cache_read(cache_file, &rows)) >= 0) {
        pthread_mutex_lock(&mu);
        if (gen == active_gen) {
            free(active_rows);
            active_rows = rows;
            active_count = count;
            active_cap = count;
            active_complete = 1;
            rows = NULL;
        }
        pthread_mutex_unlock(&mu);
        free(rows);
        return;
    }

    {
        ActiveJob job;
        int rc;
        job.gen = gen;
        rc = analyze_stream(path, active_sink, &job);
        pthread_mutex_lock(&mu);
        if (gen == active_gen) {
            /* Mark done even on failure so we don't spin on a bad file. */
            active_complete = 1;
            if (rc >= 0 && have_name) {
                spectrum_cache_write(cache_file, active_rows, active_count);
            }
        }
        pthread_mutex_unlock(&mu);
    }
}

static void run_idle_job(const char *path, unsigned gen)
{
    char cache_file[1100];
    struct stat st;
    IdleJob job;
    unsigned char *rows = NULL;
    int count;

    if (spectrum_cache_name(cache_root, path, cache_file, sizeof(cache_file)) != 0) {
        return;
    }
    if (stat(cache_file, &st) == 0) {
        return; /* already analyzed */
    }
    job.lib_gen = gen;
    job.busy_since = now_ms();
    job.aborted = 0;
    count = spectrum_analyze_file(path, &rows, idle_abort, &job);
    if (count >= 0) {
        spectrum_cache_write(cache_file, rows, count);
    } else if (!job.aborted) {
        /* Unreadable: store an empty entry so it is not retried forever. */
        spectrum_cache_write(cache_file, NULL, 0);
    } else {
        /* Interrupted: retry this track when we get back to it. */
        pthread_mutex_lock(&mu);
        if (gen == lib_gen && lib_next > 0) {
            lib_next--;
        }
        pthread_mutex_unlock(&mu);
    }
    free(rows);
}

static void *worker_main(void *arg)
{
    (void)arg;
    /* Below the UI/input threads and the mpg123 child; the idle path also
     * duty-cycles itself, so playback and the UI always win the single core. */
    set_thread_nice(10);
    for (;;) {
        char path[1024];
        unsigned gen = 0;
        int kind = 0; /* 1 active, 2 idle */

        pthread_mutex_lock(&mu);
        while (!quitting) {
            if (active_path[0] && !active_complete) {
                snprintf(path, sizeof(path), "%s", active_path);
                gen = active_gen;
                kind = 1;
                break;
            }
            if (background_on && lib_next < lib_count && now_ms() - last_input_ms >= INPUT_BACKOFF_MS) {
                snprintf(path, sizeof(path), "%s", lib_paths[lib_next]);
                lib_next++;
                gen = lib_gen;
                kind = 2;
                break;
            }
            {
                struct timeval tv;
                struct timespec ts;
                gettimeofday(&tv, NULL);
                ts.tv_sec = tv.tv_sec + 1;
                ts.tv_nsec = (long)tv.tv_usec * 1000L;
                pthread_cond_timedwait(&cv, &mu, &ts);
            }
        }
        pthread_mutex_unlock(&mu);
        if (quitting) {
            break;
        }
        if (kind == 1) {
            run_active_job(path, gen);
        } else if (kind == 2) {
            run_idle_job(path, gen);
        }
    }
    return NULL;
}

int spectrum_init(const char *cache_dir)
{
    if (started) {
        return 0;
    }
    snprintf(cache_root, sizeof(cache_root), "%s", cache_dir ? cache_dir : "cache");
    if (mkdir(cache_root, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "spectrum: cannot create %s\n", cache_root);
    }
    quitting = 0;
    if (pthread_create(&worker, NULL, worker_main, NULL) != 0) {
        fprintf(stderr, "spectrum: worker thread failed\n");
        return -1;
    }
    started = 1;
    return 0;
}

void spectrum_shutdown(void)
{
    int i;

    if (!started) {
        return;
    }
    pthread_mutex_lock(&mu);
    quitting = 1;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
    pthread_join(worker, NULL);
    started = 0;

    free(active_rows);
    active_rows = NULL;
    active_count = active_cap = 0;
    active_path[0] = '\0';
    for (i = 0; i < lib_count; i++) {
        free(lib_paths[i]);
    }
    free(lib_paths);
    lib_paths = NULL;
    lib_count = lib_next = 0;
}

void spectrum_set_background(int on)
{
    pthread_mutex_lock(&mu);
    background_on = on ? 1 : 0;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
}

void spectrum_set_library(const char *const *paths, int count)
{
    char **copy = NULL;
    int n = 0;
    int i;

    if (count > 0) {
        copy = (char **)calloc((size_t)count, sizeof(char *));
        if (copy) {
            for (i = 0; i < count; i++) {
                if (paths[i] && paths[i][0]) {
                    size_t len = strlen(paths[i]) + 1;
                    copy[n] = (char *)malloc(len);
                    if (copy[n]) {
                        memcpy(copy[n], paths[i], len);
                        n++;
                    }
                }
            }
        }
    }
    pthread_mutex_lock(&mu);
    for (i = 0; i < lib_count; i++) {
        free(lib_paths[i]);
    }
    free(lib_paths);
    lib_paths = copy;
    lib_count = n;
    lib_next = 0;
    lib_gen++;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
}

void spectrum_set_active(const char *path)
{
    if (!path) {
        path = "";
    }
    pthread_mutex_lock(&mu);
    if (strcmp(active_path, path) != 0) {
        snprintf(active_path, sizeof(active_path), "%s", path);
        active_gen++;
        free(active_rows);
        active_rows = NULL;
        active_count = active_cap = 0;
        active_complete = 0;
        active_requested_ms = 0;
        pthread_cond_broadcast(&cv);
    }
    pthread_mutex_unlock(&mu);
}

void spectrum_note_input(void)
{
    pthread_mutex_lock(&mu);
    last_input_ms = now_ms();
    pthread_mutex_unlock(&mu);
}

int spectrum_sample(int ms, unsigned char *bands, int nbands)
{
    int ok = 0;

    if (nbands > SPECTRUM_BANDS) {
        nbands = SPECTRUM_BANDS;
    }
    if (ms < 0) {
        ms = 0;
    }
    pthread_mutex_lock(&mu);
    active_requested_ms = ms;
    if (active_rows && active_count > 1) {
        /* Row r covers the analysis window ending at (r + 1) hops. */
        int pos256 = (int)(((long long)ms * SPECTRUM_FPS * 256) / 1000);
        int r = pos256 >> 8;
        int frac = pos256 & 255;
        if (r + 1 < active_count) {
            const unsigned char *a = active_rows + (size_t)r * SPECTRUM_BANDS;
            const unsigned char *b = a + SPECTRUM_BANDS;
            int i;
            for (i = 0; i < nbands; i++) {
                bands[i] = (unsigned char)((a[i] * (256 - frac) + b[i] * frac) >> 8);
            }
            ok = 1;
        }
    }
    pthread_mutex_unlock(&mu);
    return ok;
}
