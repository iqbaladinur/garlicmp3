#ifndef FLAC_DSP_H
#define FLAC_DSP_H

#include <stdint.h>

/* FLAC output stage: 3-band EQ (matching mpg123's SEQ split: bass below
 * ~689 Hz, mid ~689-1378 Hz, treble above), the same anti-clip headroom as
 * the MP3 path, and ReplayGain track gain; then 16-bit output with TPDF
 * dither. Fixed-point only (the device toolchain may be soft-float).
 *
 * When EQ is flat and no gain applies, flac_dsp_is_bypass() is true and
 * callers keep the bit-exact path for 16-bit sources. */

#define FLAC_DSP_MAX_CH 2

typedef struct FlacDsp {
    int active;               /* 0 = bypass */
    int sample_rate;
    /* Biquad coefficients, Q28. */
    int32_t lb0, lb1, lb2, la1, la2; /* low shelf  */
    int32_t hb0, hb1, hb2, ha1, ha2; /* high shelf */
    int32_t gain_q28;                /* mid * headroom * replaygain */
    int lo_on;
    int hi_on;
    /* Per-channel state, 24-bit-scale samples (int32). */
    int32_t lx1[FLAC_DSP_MAX_CH], lx2[FLAC_DSP_MAX_CH], ly1[FLAC_DSP_MAX_CH], ly2[FLAC_DSP_MAX_CH];
    int32_t hx1[FLAC_DSP_MAX_CH], hx2[FLAC_DSP_MAX_CH], hy1[FLAC_DSP_MAX_CH], hy2[FLAC_DSP_MAX_CH];
    uint32_t rng;
} FlacDsp;

/* bass/mid/treble in tenths (10 = 1.0x, like settings.c); replaygain_db
 * (0 when off or untagged); peak (linear, 0 when unknown) limits the
 * ReplayGain boost so the track peak never exceeds full scale. */
void flac_dsp_setup(FlacDsp *d, int sample_rate, int bass_t, int mid_t, int treble_t,
                    float replaygain_db, float peak);
int flac_dsp_is_bypass(const FlacDsp *d);
void flac_dsp_reset(FlacDsp *d);

/* in: interleaved samples at 24-bit scale (s16 << 8, or s32 >> 8).
 * out: interleaved s16, dithered. */
void flac_dsp_process(FlacDsp *d, const int32_t *in, int16_t *out, int frames, int channels);

/* TPDF-dithered 24-bit-scale -> s16 (no EQ), for deep sources in bypass. */
int16_t flac_dsp_dither16(FlacDsp *d, int32_t s24);

#endif
