#ifndef AUDIO_FLAC_H
#define AUDIO_FLAC_H

#include "audio_mpg123.h" /* AudioState */

/* FLAC playback path, fully separate from the mpg123 MP3 path.
 *
 * A decoder thread (dr_flac) writes interleaved s16 PCM into a pipe read by an
 * `out123` child process (ships with mpg123, same audio output stack). 16-bit
 * sources are passed through untouched when EQ is flat and no ReplayGain
 * applies; otherwise (and for deeper sources) the signal goes through
 * flac_dsp (EQ / gain at 24-bit scale) and is TPDF-dithered to 16-bit.
 * Consecutive tracks with the same format reuse the running out123, so album
 * playback is gapless. */

int flac_is_path(const char *path);

/* Start playback at start_seconds. Returns 0 on success, -1 with
 * flac_last_error() set (e.g. unreadable file, out123 missing). */
int flac_play(const char *path, int start_seconds);

/* graceful: let already-decoded audio drain (end of playlist);
 * otherwise cut immediately (user stop, or handing the device to mpg123). */
void flac_stop(int graceful);

void flac_pause_toggle(void);
AudioState flac_state(void);
void flac_poll(void);
int flac_elapsed_ms(void);
int flac_duration_seconds(void);
int flac_take_finished(void);
/* Same tenths scale as audio_set_eq; RVA uses REPLAYGAIN_TRACK_GAIN/PEAK.
 * Flat EQ + no gain keeps 16-bit sources bit-exact. */
void flac_set_eq(int bass_tenths, int mid_tenths, int treble_tenths);
void flac_set_rva(int on);
const char *flac_last_error(void);
void flac_shutdown(void);

#endif
