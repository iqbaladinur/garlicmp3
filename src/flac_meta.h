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
} FlacMeta;

/* Returns 1 when the file is a FLAC stream (STREAMINFO found). Tag fields are
 * empty when there is no Vorbis comment block. */
int flac_meta_read(const char *path, FlacMeta *out);

/* Same, parsing an in-memory file prefix (for tests). */
int flac_meta_parse(const unsigned char *data, size_t size, FlacMeta *out);

int flac_meta_duration_seconds(const FlacMeta *m);

#endif
