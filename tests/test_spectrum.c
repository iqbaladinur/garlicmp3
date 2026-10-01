#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spectrum.h"

#define RATE 44100

static int peak_band(const unsigned char *row)
{
    int i;
    int best = 0;
    for (i = 1; i < SPECTRUM_BANDS; i++) {
        if (row[i] > row[best]) {
            best = i;
        }
    }
    return best;
}

static int feed_tone(SpectrumAnalyzer *a, float hz, float amp, int seconds, unsigned char *rows, int max_rows)
{
    short pcm[2 * 1152];
    int total = 0;
    int n = 0;
    int left = RATE * seconds;

    while (left > 0) {
        int chunk = left < 1152 ? left : 1152;
        int i;
        for (i = 0; i < chunk; i++, n++) {
            short v = (short)(amp * 32767.0f * sinf(2.0f * 3.14159265f * hz * (float)n / RATE));
            pcm[i * 2] = v;
            pcm[i * 2 + 1] = v;
        }
        total += spectrum_analyzer_feed(a, pcm, chunk, 2, rows + total * SPECTRUM_BANDS, max_rows - total);
        left -= chunk;
    }
    return total;
}

static void test_rows_per_second(void)
{
    SpectrumAnalyzer *a = spectrum_analyzer_new(RATE);
    unsigned char rows[64 * SPECTRUM_BANDS];
    int n = feed_tone(a, 440.0f, 0.5f, 2, rows, 64);
    assert(n == 2 * SPECTRUM_FPS);
    spectrum_analyzer_free(a);
}

static void test_tone_lands_in_right_band(void)
{
    SpectrumAnalyzer *lo = spectrum_analyzer_new(RATE);
    SpectrumAnalyzer *hi = spectrum_analyzer_new(RATE);
    unsigned char rows[64 * SPECTRUM_BANDS];
    int n;
    int b_lo;
    int b_hi;

    n = feed_tone(lo, 100.0f, 0.5f, 1, rows, 64);
    b_lo = peak_band(rows + (n - 1) * SPECTRUM_BANDS);
    n = feed_tone(hi, 5000.0f, 0.5f, 1, rows, 64);
    b_hi = peak_band(rows + (n - 1) * SPECTRUM_BANDS);
    printf("100 Hz -> band %d, 5 kHz -> band %d\n", b_lo, b_hi);
    assert(b_lo <= 5);
    assert(b_hi >= 16 && b_hi <= 21);
    assert(rows[(n - 1) * SPECTRUM_BANDS + b_hi] > 150);
    spectrum_analyzer_free(lo);
    spectrum_analyzer_free(hi);
}

static void test_silence_is_zero(void)
{
    SpectrumAnalyzer *a = spectrum_analyzer_new(RATE);
    unsigned char rows[64 * SPECTRUM_BANDS];
    int n = feed_tone(a, 1000.0f, 0.0f, 1, rows, 64);
    int i;
    for (i = 0; i < n * SPECTRUM_BANDS; i++) {
        assert(rows[i] == 0);
    }
    spectrum_analyzer_free(a);
}

static void test_cache_roundtrip(void)
{
    const char *dir = "build";
    const char *track = "build/test_spectrum_track.bin";
    char name[1100];
    char name2[1100];
    unsigned char rows[3 * SPECTRUM_BANDS];
    unsigned char *back = NULL;
    FILE *fp;
    int i;

    fp = fopen(track, "wb");
    assert(fp);
    fputs("not really an mp3", fp);
    fclose(fp);

    assert(spectrum_cache_name(dir, track, name, sizeof(name)) == 0);
    assert(spectrum_cache_name(dir, track, name2, sizeof(name2)) == 0);
    assert(strcmp(name, name2) == 0);
    assert(spectrum_cache_name(dir, "build/does-not-exist.mp3", name2, sizeof(name2)) != 0);

    for (i = 0; i < 3 * SPECTRUM_BANDS; i++) {
        rows[i] = (unsigned char)(i * 7);
    }
    assert(spectrum_cache_write(name, rows, 3) == 0);
    assert(spectrum_cache_read(name, &back) == 3);
    assert(memcmp(rows, back, sizeof(rows)) == 0);
    free(back);

    /* Empty entry marks an unreadable track. */
    assert(spectrum_cache_write(name, NULL, 0) == 0);
    assert(spectrum_cache_read(name, &back) == 0);
    assert(back == NULL);

    /* Non-MP3 data decodes to zero rows rather than failing. */
    assert(spectrum_analyze_file(track, &back, NULL, NULL) == 0);
    free(back);

    remove(name);
    remove(track);
}

int main(void)
{
    test_rows_per_second();
    test_tone_lands_in_right_band();
    test_silence_is_zero();
    test_cache_roundtrip();
    printf("test_spectrum OK\n");
    return 0;
}
