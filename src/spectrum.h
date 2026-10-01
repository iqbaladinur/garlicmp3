#ifndef SPECTRUM_H
#define SPECTRUM_H

#include <stddef.h>

/* Real spectrum data for the now-playing visualizer.
 *
 * Playback is untouched (mpg123 for MP3, out123 for FLAC); this module decodes
 * the same file a second time (MP3: minimp3, FLAC: dr_flac) on a background
 * thread, reduces it to SPECTRUM_BANDS log-spaced band levels at
 * SPECTRUM_FPS, and caches the result on disk so every track is analyzed only
 * once.
 *
 *  - On demand: the active (playing) track is analyzed first, at full speed,
 *    as soon as it starts playing. Until the analysis has caught up with the
 *    playhead, spectrum_sample() returns 0 and the UI keeps its synthesized
 *    animation.
 *  - Background: when the active track is done, the rest of the library is
 *    analyzed one track at a time, niced and throttled, pausing while the
 *    user is pressing buttons.
 */

#define SPECTRUM_BANDS 24
#define SPECTRUM_FPS 25

/* Start the worker. cache_dir is created if missing. Returns 0 on success. */
int spectrum_init(const char *cache_dir);
void spectrum_shutdown(void);

/* Enable/disable background analysis of the rest of the library. */
void spectrum_set_background(int on);

/* Library paths for background analysis (copied). */
void spectrum_set_library(const char *const *paths, int count);

/* Track whose levels spectrum_sample() returns; NULL/"" for none. Cheap to
 * call every frame with the same path. */
void spectrum_set_active(const char *path);

/* Note user input; background analysis backs off for a few seconds. */
void spectrum_note_input(void);

/* Band levels (0..255) for the active track at playback time ms, linearly
 * interpolated between analysis frames. Returns 1 when data is available. */
int spectrum_sample(int ms, unsigned char *bands, int nbands);

/* ---- Pure helpers, exposed for tests ---- */

typedef struct SpectrumAnalyzer SpectrumAnalyzer;

/* Streaming PCM -> band frames. Feed interleaved 16-bit PCM; every hop of
 * sample_rate / SPECTRUM_FPS input frames emits one SPECTRUM_BANDS row. */
SpectrumAnalyzer *spectrum_analyzer_new(int sample_rate);
void spectrum_analyzer_free(SpectrumAnalyzer *a);
/* Returns the number of rows written to out (capacity out_rows rows). */
int spectrum_analyzer_feed(SpectrumAnalyzer *a, const short *pcm, int frames, int channels,
                           unsigned char *out, int out_rows);

/* Decode + analyze an MP3 or FLAC file into a malloc'd SPECTRUM_BANDS * frames
 * buffer. abort_fn (optional) is polled between decoded chunks; returning nonzero stops
 * the analysis and the function returns -1. Returns the row count, or -1. */
int spectrum_analyze_file(const char *path, unsigned char **out_rows,
                          int (*abort_fn)(void *ctx, int rows_done), void *ctx);

/* Cache file name for a track (hash of path, size and mtime). Returns 0 when
 * the track file exists. */
int spectrum_cache_name(const char *cache_dir, const char *track_path, char *out, size_t out_size);
int spectrum_cache_write(const char *cache_file, const unsigned char *rows, int count);
/* Returns rows (>= 0) and a malloc'd buffer (NULL when 0 rows), or -1. */
int spectrum_cache_read(const char *cache_file, unsigned char **rows);

#endif
