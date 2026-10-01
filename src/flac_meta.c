#include "flac_meta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define BLOCK_STREAMINFO 0
#define BLOCK_VORBIS_COMMENT 4
#define BLOCK_PICTURE 6
#define MAX_PICTURE_BLOCK (16 * 1024 * 1024)
#define MAX_COMMENT_BLOCK (256 * 1024)

static unsigned long le32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static void parse_streaminfo(const unsigned char *b, size_t len, FlacMeta *m)
{
    if (len < 18) {
        return;
    }
    /* Bytes 10..17: 20-bit rate, 3-bit channels-1, 5-bit bps-1, 36-bit frames. */
    m->sample_rate = (int)(((unsigned long)b[10] << 12) | ((unsigned long)b[11] << 4) | (b[12] >> 4));
    m->channels = ((b[12] >> 1) & 7) + 1;
    m->bits_per_sample = (((b[12] & 1) << 4) | (b[13] >> 4)) + 1;
    m->total_frames = ((unsigned long long)(b[13] & 0x0f) << 32) |
                      ((unsigned long long)b[14] << 24) | ((unsigned long long)b[15] << 16) |
                      ((unsigned long long)b[16] << 8) | (unsigned long long)b[17];
}

static void copy_field(char *out, size_t out_size, const char *val, size_t len)
{
    if (out[0]) {
        return; /* keep the first occurrence */
    }
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, val, len);
    out[len] = '\0';
}

static void copy_num(char *out, size_t out_size, const char *val, size_t len)
{
    if (len >= out_size) {
        len = out_size - 1;
    }
    memcpy(out, val, len);
    out[len] = '\0';
}

static void parse_vorbis_comment(const unsigned char *b, size_t len, FlacMeta *m)
{
    size_t pos = 0;
    unsigned long vendor_len;
    unsigned long count;
    unsigned long i;

    if (len < 8) {
        return;
    }
    vendor_len = le32(b);
    pos = 4 + vendor_len;
    if (pos + 4 > len) {
        return;
    }
    count = le32(b + pos);
    pos += 4;
    for (i = 0; i < count && pos + 4 <= len; i++) {
        unsigned long clen = le32(b + pos);
        const char *c = (const char *)(b + pos + 4);
        pos += 4;
        if (clen > len - pos) {
            break;
        }
        if (clen > 6 && strncasecmp(c, "TITLE=", 6) == 0) {
            copy_field(m->title, sizeof(m->title), c + 6, clen - 6);
        } else if (clen > 7 && strncasecmp(c, "ARTIST=", 7) == 0) {
            copy_field(m->artist, sizeof(m->artist), c + 7, clen - 7);
        } else if (clen > 22 && strncasecmp(c, "REPLAYGAIN_TRACK_GAIN=", 22) == 0) {
            char num[32];
            copy_num(num, sizeof(num), c + 22, clen - 22);
            m->track_gain_db = (float)strtod(num, NULL); /* "-6.48 dB" */
            m->has_track_gain = 1;
        } else if (clen > 22 && strncasecmp(c, "REPLAYGAIN_TRACK_PEAK=", 22) == 0) {
            char num[32];
            copy_num(num, sizeof(num), c + 22, clen - 22);
            m->track_peak = (float)strtod(num, NULL);
        }
        pos += clen;
    }
}

/* Some taggers put an ID3v2 block in front of "fLaC"; skip it. */
static size_t id3_skip(const unsigned char *h, size_t have)
{
    if (have >= 10 && memcmp(h, "ID3", 3) == 0) {
        return 10 + (((size_t)(h[6] & 0x7f) << 21) | ((size_t)(h[7] & 0x7f) << 14) |
                     ((size_t)(h[8] & 0x7f) << 7) | (size_t)(h[9] & 0x7f));
    }
    return 0;
}

int flac_meta_parse(const unsigned char *data, size_t size, FlacMeta *out)
{
    size_t pos;
    int have_info = 0;

    memset(out, 0, sizeof(*out));
    pos = id3_skip(data, size);
    if (pos + 4 > size || memcmp(data + pos, "fLaC", 4) != 0) {
        return 0;
    }
    pos += 4;
    while (pos + 4 <= size) {
        int last = (data[pos] & 0x80) != 0;
        int type = data[pos] & 0x7f;
        size_t len = ((size_t)data[pos + 1] << 16) | ((size_t)data[pos + 2] << 8) | data[pos + 3];
        pos += 4;
        if (len > size - pos) {
            break;
        }
        if (type == BLOCK_STREAMINFO) {
            parse_streaminfo(data + pos, len, out);
            have_info = out->sample_rate > 0;
        } else if (type == BLOCK_VORBIS_COMMENT) {
            parse_vorbis_comment(data + pos, len, out);
        }
        pos += len;
        if (last) {
            break;
        }
    }
    return have_info;
}

int flac_meta_read(const char *path, FlacMeta *out)
{
    FILE *fp;
    unsigned char h[10];
    long pos = 0;
    int have_info = 0;

    memset(out, 0, sizeof(*out));
    fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    if (fread(h, 1, sizeof(h), fp) == sizeof(h)) {
        pos = (long)id3_skip(h, sizeof(h));
    }
    if (fseek(fp, pos, SEEK_SET) != 0 || fread(h, 1, 4, fp) != 4 || memcmp(h, "fLaC", 4) != 0) {
        fclose(fp);
        return 0;
    }

    /* Walk the blocks, reading only STREAMINFO and VORBIS_COMMENT bodies;
     * everything else (pictures, seek tables, padding) is skipped. */
    for (;;) {
        unsigned char bh[4];
        int last;
        int type;
        size_t len;

        if (fread(bh, 1, 4, fp) != 4) {
            break;
        }
        last = (bh[0] & 0x80) != 0;
        type = bh[0] & 0x7f;
        len = ((size_t)bh[1] << 16) | ((size_t)bh[2] << 8) | bh[3];
        if ((type == BLOCK_STREAMINFO || type == BLOCK_VORBIS_COMMENT) && len <= MAX_COMMENT_BLOCK) {
            unsigned char *body = (unsigned char *)malloc(len ? len : 1);
            if (!body || fread(body, 1, len, fp) != len) {
                free(body);
                break;
            }
            if (type == BLOCK_STREAMINFO) {
                parse_streaminfo(body, len, out);
                have_info = out->sample_rate > 0;
            } else {
                parse_vorbis_comment(body, len, out);
            }
            free(body);
        } else if (fseek(fp, (long)len, SEEK_CUR) != 0) {
            break;
        }
        if (last) {
            break;
        }
    }
    fclose(fp);
    return have_info;
}

int flac_meta_duration_seconds(const FlacMeta *m)
{
    if (!m || m->sample_rate <= 0 || m->total_frames == 0) {
        return 0;
    }
    return (int)((m->total_frames + (unsigned long long)m->sample_rate / 2) / (unsigned long long)m->sample_rate);
}

static unsigned long be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

int flac_meta_parse_picture(const unsigned char *b, unsigned long len,
                            const unsigned char **img, unsigned long *img_len, int *pic_type)
{
    unsigned long pos = 0;
    unsigned long n;

    if (len < 32) {
        return 0;
    }
    *pic_type = (int)be32(b);
    pos = 4;
    n = be32(b + pos);                /* MIME */
    if (n > len - pos - 4) {
        return 0;
    }
    pos += 4 + n;
    if (pos + 4 > len) {
        return 0;
    }
    n = be32(b + pos);                /* description */
    if (n > len - pos - 4) {
        return 0;
    }
    pos += 4 + n;
    if (pos + 20 > len) {             /* width, height, depth, colors, length */
        return 0;
    }
    n = be32(b + pos + 16);
    pos += 20;
    if (n == 0 || n > len - pos) {
        return 0;
    }
    *img = b + pos;
    *img_len = n;
    return 1;
}

int flac_meta_read_picture(const char *path, unsigned char **data, unsigned long *len)
{
    FILE *fp;
    unsigned char h[10];
    long pos = 0;
    int found = 0;

    *data = NULL;
    *len = 0;
    fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    if (fread(h, 1, sizeof(h), fp) == sizeof(h)) {
        pos = (long)id3_skip(h, sizeof(h));
    }
    if (fseek(fp, pos, SEEK_SET) != 0 || fread(h, 1, 4, fp) != 4 || memcmp(h, "fLaC", 4) != 0) {
        fclose(fp);
        return 0;
    }
    for (;;) {
        unsigned char bh[4];
        int last;
        int type;
        size_t blen;

        if (fread(bh, 1, 4, fp) != 4) {
            break;
        }
        last = (bh[0] & 0x80) != 0;
        type = bh[0] & 0x7f;
        blen = ((size_t)bh[1] << 16) | ((size_t)bh[2] << 8) | bh[3];
        if (type == BLOCK_PICTURE && blen <= MAX_PICTURE_BLOCK) {
            unsigned char *body = (unsigned char *)malloc(blen ? blen : 1);
            const unsigned char *img;
            unsigned long img_len;
            int pic_type = 0;
            if (!body || fread(body, 1, blen, fp) != blen) {
                free(body);
                break;
            }
            if (flac_meta_parse_picture(body, (unsigned long)blen, &img, &img_len, &pic_type)) {
                /* Keep the first picture, but let a front cover (3) win. */
                if (!found || pic_type == 3) {
                    free(*data);
                    *data = (unsigned char *)malloc(img_len);
                    if (*data) {
                        memcpy(*data, img, img_len);
                        *len = img_len;
                        found = 1;
                    }
                }
            }
            free(body);
            if (found && pic_type == 3) {
                break;
            }
        } else if (fseek(fp, (long)blen, SEEK_CUR) != 0) {
            break;
        }
        if (last) {
            break;
        }
    }
    fclose(fp);
    return found;
}
