#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "flac_meta.h"

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
    static const char *c[] = { "title=Gymnopedie No.1", "ARTIST=Erik Satie", "TITLE=ignored duplicate" };
    unsigned char body[256];
    size_t b = 0;
    size_t i;

    b += put_le32(body + b, (unsigned long)strlen(vendor));
    memcpy(body + b, vendor, strlen(vendor));
    b += strlen(vendor);
    b += put_le32(body + b, 3);
    for (i = 0; i < 3; i++) {
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

    printf("test_flac OK\n");
    return 0;
}
