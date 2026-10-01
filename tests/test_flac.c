#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "flac_dsp.h"
#include "flac_meta.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static size_t put_block_header(unsigned char *p, int last, int type, size_t len)
{
    p[0] = (unsigned char)((last ? 0x80 : 0) | type);
    p[1] = (unsigned char)(len >> 16);
    p[2] = (unsigned char)(len >> 8);
    p[3] = (unsigned char)len;
    return 4;
}

static size_t put_le32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
    return 4;
}

/* STREAMINFO for 24-bit / 96 kHz / stereo / 9,600,000 frames (100 s). */
static size_t put_streaminfo(unsigned char *p, int last)
{
    unsigned long rate = 96000;
    unsigned ch = 2;
    unsigned bps = 24;
    unsigned long long frames = 9600000ULL;
    size_t n = put_block_header(p, last, 0, 34);
    memset(p + n, 0, 34);
    p[n + 10] = (unsigned char)(rate >> 12);
    p[n + 11] = (unsigned char)(rate >> 4);
    p[n + 12] = (unsigned char)(((rate & 0x0f) << 4) | ((ch - 1) << 1) | ((bps - 1) >> 4));
    p[n + 13] = (unsigned char)((((bps - 1) & 0x0f) << 4) | (unsigned)((frames >> 32) & 0x0f));
    p[n + 14] = (unsigned char)(frames >> 24);
    p[n + 15] = (unsigned char)(frames >> 16);
    p[n + 16] = (unsigned char)(frames >> 8);
    p[n + 17] = (unsigned char)frames;
    return n + 34;
}

static size_t put_comments(unsigned char *p, int last)
{
    static const char *vendor = "test";
    static const char *c[] = { "title=Gymnopedie No.1", "ARTIST=Erik Satie", "TITLE=ignored duplicate",
                               "REPLAYGAIN_TRACK_GAIN=-6.48 dB", "replaygain_track_peak=0.988525" };
    unsigned char body[256];
    size_t b = 0;
    size_t i;

    b += put_le32(body + b, (unsigned long)strlen(vendor));
    memcpy(body + b, vendor, strlen(vendor));
    b += strlen(vendor);
    b += put_le32(body + b, 5);
    for (i = 0; i < 5; i++) {
        b += put_le32(body + b, (unsigned long)strlen(c[i]));
        memcpy(body + b, c[i], strlen(c[i]));
        b += strlen(c[i]);
    }
    put_block_header(p, last, 4, b);
    memcpy(p + 4, body, b);
    return 4 + b;
}

static size_t build(unsigned char *buf, int with_id3, int with_padding)
{
    size_t n = 0;
    if (with_id3) {
        static const unsigned char id3[10] = { 'I', 'D', '3', 3, 0, 0, 0, 0, 0, 20 };
        memcpy(buf, id3, 10);
        memset(buf + 10, 0, 20);
        n = 30;
    }
    memcpy(buf + n, "fLaC", 4);
    n += 4;
    n += put_streaminfo(buf + n, 0);
    if (with_padding) {
        n += put_block_header(buf + n, 0, 1, 100);
        memset(buf + n, 0, 100);
        n += 100;
    }
    n += put_comments(buf + n, 1);
    return n;
}

static void check(const FlacMeta *m)
{
    assert(m->sample_rate == 96000);
    assert(m->channels == 2);
    assert(m->bits_per_sample == 24);
    assert(m->total_frames == 9600000ULL);
    assert(flac_meta_duration_seconds(m) == 100);
    assert(strcmp(m->title, "Gymnopedie No.1") == 0);
    assert(strcmp(m->artist, "Erik Satie") == 0);
    assert(m->has_track_gain == 1);
    assert(fabsf(m->track_gain_db - (-6.48f)) < 0.001f);
    assert(fabsf(m->track_peak - 0.988525f) < 0.0001f);
}

static void test_picture(void)
{
    /* type=3, mime "image/png", desc "", 4 dims, then 5 data bytes. */
    unsigned char b[64];
    const unsigned char *img;
    unsigned long img_len;
    int type;
    size_t n = 0;
    static const unsigned char be3[4] = { 0, 0, 0, 3 };
    static const unsigned char be9[4] = { 0, 0, 0, 9 };
    static const unsigned char be0[4] = { 0, 0, 0, 0 };
    static const unsigned char be5[4] = { 0, 0, 0, 5 };
    int i;

    memcpy(b + n, be3, 4); n += 4;
    memcpy(b + n, be9, 4); n += 4;
    memcpy(b + n, "image/png", 9); n += 9;
    memcpy(b + n, be0, 4); n += 4;
    for (i = 0; i < 4; i++) {
        memcpy(b + n, be0, 4); n += 4;
    }
    memcpy(b + n, be5, 4); n += 4;
    memcpy(b + n, "ABCDE", 5); n += 5;
    assert(flac_meta_parse_picture(b, (unsigned long)n, &img, &img_len, &type) == 1);
    assert(type == 3 && img_len == 5 && memcmp(img, "ABCDE", 5) == 0);
    assert(flac_meta_parse_picture(b, (unsigned long)n - 1, &img, &img_len, &type) == 0);
}

/* Output amplitude (s16) of a sine through the DSP, measured as RMS * sqrt(2)
 * over the settled second half (sample peaks miss the crest at high
 * frequencies, e.g. 8 kHz at 48 kHz has only 6 samples per cycle). */
static int dsp_peak(FlacDsp *d, double hz, double amp)
{
    enum { N = 48000 };
    static int32_t in[N * 2];
    static int16_t out[N * 2];
    int i;
    double acc = 0.0;
    for (i = 0; i < N; i++) {
        int32_t v = (int32_t)(amp * 8388607.0 * sin(2.0 * 3.14159265358979 * hz * i / 48000.0));
        in[i * 2] = v;
        in[i * 2 + 1] = v;
    }
    flac_dsp_reset(d);
    flac_dsp_process(d, in, out, N, 2);
    for (i = N; i < N * 2; i += 2) {
        acc += (double)out[i] * (double)out[i];
    }
    return (int)(sqrt(acc / (N / 2)) * sqrt(2.0) + 0.5);
}

static void test_dsp(void)
{
    FlacDsp d;
    int p_flat;
    int p_lo;
    int p_hi;

    memset(&d, 0, sizeof(d));
    flac_dsp_setup(&d, 48000, 10, 10, 10, 0.0f, 0.0f);
    assert(flac_dsp_is_bypass(&d));

    /* ReplayGain -6.02 dB halves the level. */
    flac_dsp_setup(&d, 48000, 10, 10, 10, -6.0206f, 0.0f);
    assert(!flac_dsp_is_bypass(&d));
    p_flat = dsp_peak(&d, 1000.0, 0.5);
    printf("rg -6dB: peak %d (expect ~8192)\n", p_flat);
    assert(abs(p_flat - 8192) < 60);

    /* +10 dB with a 0.9 peak is limited to 1/0.9. */
    flac_dsp_setup(&d, 48000, 10, 10, 10, 10.0f, 0.9f);
    p_flat = dsp_peak(&d, 1000.0, 0.5);
    printf("rg +10dB peak-limited: %d (expect ~%d)\n", p_flat, (int)(16384 / 0.9));
    assert(abs(p_flat - (int)(16384 / 0.9)) < 80);

    /* Bass x2: 100 Hz goes up ~2x before headroom (71%), 8 kHz only gets
     * the headroom cut. */
    flac_dsp_setup(&d, 48000, 20, 10, 10, 0.0f, 0.0f);
    p_lo = dsp_peak(&d, 100.0, 0.25);
    p_hi = dsp_peak(&d, 8000.0, 0.25);
    printf("bass x2: 100 Hz %d, 8 kHz %d (input 8192)\n", p_lo, p_hi);
    assert(p_lo > 8192 * 13 / 10 && p_lo < 8192 * 15 / 10);
    assert(abs(p_hi - 8192 * 71 / 100) < 120);

    /* Treble x2: the reverse. */
    flac_dsp_setup(&d, 48000, 10, 10, 20, 0.0f, 0.0f);
    p_lo = dsp_peak(&d, 100.0, 0.25);
    p_hi = dsp_peak(&d, 8000.0, 0.25);
    printf("treble x2: 100 Hz %d, 8 kHz %d\n", p_lo, p_hi);
    assert(abs(p_lo - 8192 * 71 / 100) < 120);
    assert(p_hi > 8192 * 13 / 10 && p_hi < 8192 * 15 / 10);
}

int main(void)
{
    unsigned char buf[1024];
    FlacMeta m;
    size_t n;
    FILE *fp;

    n = build(buf, 0, 0);
    assert(flac_meta_parse(buf, n, &m) == 1);
    check(&m);

    n = build(buf, 1, 1);
    assert(flac_meta_parse(buf, n, &m) == 1);
    check(&m);

    /* Same file through the streaming reader (skips the padding block). */
    fp = fopen("build/test_meta.flac", "wb");
    assert(fp);
    fwrite(buf, 1, n, fp);
    fclose(fp);
    assert(flac_meta_read("build/test_meta.flac", &m) == 1);
    check(&m);
    remove("build/test_meta.flac");

    /* Not FLAC. */
    assert(flac_meta_parse((const unsigned char *)"ID3\3\0\0\0\0\0\0RIFF....", 18, &m) == 0);
    assert(flac_meta_read("build/does-not-exist.flac", &m) == 0);

    test_picture();
    test_dsp();
    printf("test_flac OK\n");
    return 0;
}
