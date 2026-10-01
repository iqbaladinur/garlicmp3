#include "flac_dsp.h"

#include <math.h>
#include <string.h>

#include "audio_mpg123.h" /* audio_headroom_volume_pct: same curve as MP3 */

#define Q 28
#define ONE_Q ((int32_t)1 << Q)
/* mpg123's SEQ edges at 44.1 kHz (subband 0 / 1 / 2..31). Kept fixed in Hz
 * so the EQ sounds the same at any FLAC sample rate. */
#define EQ_LOW_HZ 689.0
#define EQ_HIGH_HZ 1378.0

static int32_t to_q(double v)
{
    double s = v * (double)ONE_Q;
    if (s > 2147483647.0) {
        s = 2147483647.0;
    }
    if (s < -2147483648.0) {
        s = -2147483648.0;
    }
    return (int32_t)(s < 0 ? s - 0.5 : s + 0.5);
}

/* RBJ cookbook shelving filters, S = 1. */
static void shelf(int high, double fs, double f0, double gain_lin,
                  int32_t *b0, int32_t *b1, int32_t *b2, int32_t *a1, int32_t *a2)
{
    double A = sqrt(gain_lin);
    double w0 = 2.0 * 3.14159265358979 * f0 / fs;
    double cw = cos(w0);
    double alpha = sin(w0) / 2.0 * sqrt(2.0);
    double sq = 2.0 * sqrt(A) * alpha;
    double nb0, nb1, nb2, na0, na1, na2;

    if (!high) {
        nb0 = A * ((A + 1) - (A - 1) * cw + sq);
        nb1 = 2 * A * ((A - 1) - (A + 1) * cw);
        nb2 = A * ((A + 1) - (A - 1) * cw - sq);
        na0 = (A + 1) + (A - 1) * cw + sq;
        na1 = -2 * ((A - 1) + (A + 1) * cw);
        na2 = (A + 1) + (A - 1) * cw - sq;
    } else {
        nb0 = A * ((A + 1) + (A - 1) * cw + sq);
        nb1 = -2 * A * ((A - 1) + (A + 1) * cw);
        nb2 = A * ((A + 1) + (A - 1) * cw - sq);
        na0 = (A + 1) - (A - 1) * cw + sq;
        na1 = 2 * ((A - 1) - (A + 1) * cw);
        na2 = (A + 1) - (A - 1) * cw - sq;
    }
    *b0 = to_q(nb0 / na0);
    *b1 = to_q(nb1 / na0);
    *b2 = to_q(nb2 / na0);
    *a1 = to_q(na1 / na0);
    *a2 = to_q(na2 / na0);
}

void flac_dsp_reset(FlacDsp *d)
{
    memset(d->lx1, 0, sizeof(d->lx1));
    memset(d->lx2, 0, sizeof(d->lx2));
    memset(d->ly1, 0, sizeof(d->ly1));
    memset(d->ly2, 0, sizeof(d->ly2));
    memset(d->hx1, 0, sizeof(d->hx1));
    memset(d->hx2, 0, sizeof(d->hx2));
    memset(d->hy1, 0, sizeof(d->hy1));
    memset(d->hy2, 0, sizeof(d->hy2));
}

void flac_dsp_setup(FlacDsp *d, int sample_rate, int bass_t, int mid_t, int treble_t,
                    float replaygain_db, float peak)
{
    double mid;
    double gain;
    uint32_t rng = d->rng ? d->rng : 0x9e3779b9u;

    memset(d, 0, sizeof(*d));
    d->rng = rng;
    d->sample_rate = sample_rate > 0 ? sample_rate : 44100;
    if (bass_t < 1) bass_t = 1;
    if (mid_t < 1) mid_t = 1;
    if (treble_t < 1) treble_t = 1;

    mid = (double)mid_t / 10.0;
    gain = mid * (double)audio_headroom_volume_pct(bass_t, mid_t, treble_t) / 100.0;
    if (replaygain_db != 0.0f) {
        double rg = pow(10.0, (double)replaygain_db / 20.0);
        if (peak > 0.0f && rg * (double)peak > 1.0) {
            rg = 1.0 / (double)peak; /* never push the track peak past 0 dBFS */
        }
        gain *= rg;
    }

    d->lo_on = bass_t != mid_t;
    d->hi_on = treble_t != mid_t;
    if (d->lo_on) {
        shelf(0, d->sample_rate, EQ_LOW_HZ, (double)bass_t / (double)mid_t,
              &d->lb0, &d->lb1, &d->lb2, &d->la1, &d->la2);
    }
    if (d->hi_on) {
        shelf(1, d->sample_rate, EQ_HIGH_HZ, (double)treble_t / (double)mid_t,
              &d->hb0, &d->hb1, &d->hb2, &d->ha1, &d->ha2);
    }
    d->gain_q28 = to_q(gain);
    d->active = d->lo_on || d->hi_on || d->gain_q28 != ONE_Q;
}

int flac_dsp_is_bypass(const FlacDsp *d)
{
    return !d->active;
}

static int32_t tpdf_24(FlacDsp *d)
{
    /* Triangular, +/- 1 LSB at 16-bit = +/- 256 at 24-bit scale. */
    uint32_t r = d->rng;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    d->rng = r;
    return (int32_t)(r & 0xff) - (int32_t)((r >> 8) & 0xff);
}

int16_t flac_dsp_dither16(FlacDsp *d, int32_t s24)
{
    int32_t v = (s24 + tpdf_24(d) + 128) >> 8;
    return (int16_t)(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}

static int32_t biquad(int32_t x, int32_t b0, int32_t b1, int32_t b2, int32_t a1, int32_t a2,
                      int32_t *x1, int32_t *x2, int32_t *y1, int32_t *y2)
{
    int64_t acc = (int64_t)b0 * x + (int64_t)b1 * *x1 + (int64_t)b2 * *x2
                - (int64_t)a1 * *y1 - (int64_t)a2 * *y2;
    int32_t y = (int32_t)(acc >> Q);
    *x2 = *x1;
    *x1 = x;
    *y2 = *y1;
    *y1 = y;
    return y;
}

void flac_dsp_process(FlacDsp *d, const int32_t *in, int16_t *out, int frames, int channels)
{
    int i;
    int c;

    for (i = 0; i < frames; i++) {
        for (c = 0; c < channels && c < FLAC_DSP_MAX_CH; c++) {
            int32_t x = in[i * channels + c];
            int64_t g;
            if (d->lo_on) {
                x = biquad(x, d->lb0, d->lb1, d->lb2, d->la1, d->la2,
                           &d->lx1[c], &d->lx2[c], &d->ly1[c], &d->ly2[c]);
            }
            if (d->hi_on) {
                x = biquad(x, d->hb0, d->hb1, d->hb2, d->ha1, d->ha2,
                           &d->hx1[c], &d->hx2[c], &d->hy1[c], &d->hy2[c]);
            }
            g = ((int64_t)x * d->gain_q28) >> Q;
            if (g > 0x7fffff) {
                g = 0x7fffff;
            } else if (g < -0x800000) {
                g = -0x800000;
            }
            out[i * channels + c] = flac_dsp_dither16(d, (int32_t)g);
        }
    }
}
