#ifndef FLAC_META_H
#define FLAC_META_H

#include <stddef.h>

/* Lightweight FLAC header reader for the library scan: walks the metadata
 * blocks only (no audio decoding). */
typedef struct FlacMeta {
    int sample_rate;
    int channels;
    int bits_per_sample;
    unsigned long long total_frames; /* 0 when unknown */
    char title[256];
    char artist[256];
    int has_track_gain;
    float track_gain_db;   /* REPLAYGAIN_TRACK_GAIN */
    float track_peak;      /* REPLAYGAIN_TRACK_PEAK, 0 when absent */
} FlacMeta;

/* Returns 1 when the file is a FLAC stream (STREAMINFO found). Tag fields are
 * empty when there is no Vorbis comment block. */
int flac_meta_read(const char *path, FlacMeta *out);

/* Same, parsing an in-memory file prefix (for tests). */
int flac_meta_parse(const unsigned char *data, size_t size, FlacMeta *out);

int flac_meta_duration_seconds(const FlacMeta *m);

/* Embedded picture (PICTURE block; front cover preferred, else the first).
 * Returns 1 with a malloc'd copy of the encoded image bytes (JPEG/PNG). */
int flac_meta_read_picture(const char *path, unsigned char **data, unsigned long *len);
int flac_meta_parse_picture(const unsigned char *block, unsigned long block_len,
                            const unsigned char **img, unsigned long *img_len, int *pic_type);

#endif
