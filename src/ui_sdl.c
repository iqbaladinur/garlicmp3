#include "ui_sdl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "album_art.h"
#include "font_cjk.h"
#include "version.h"

#define SCREEN_W 640
#define SCREEN_H 480
#define CHAR_W 8
#define CHAR_H 8
#define UI_VOLUME_MAX 40

static SDL_Surface *screen = NULL;
static SDL_Surface *background = NULL;

static void put_pixel(int x, int y, Uint32 color);

static Uint32 rgb(Uint8 r, Uint8 g, Uint8 b)
{
    return SDL_MapRGB(screen->format, r, g, b);
}

static void fill_rect(int x, int y, int w, int h, Uint32 color)
{
    SDL_Rect rect;
    rect.x = (Sint16)x;
    rect.y = (Sint16)y;
    rect.w = (Uint16)w;
    rect.h = (Uint16)h;
    SDL_FillRect(screen, &rect, color);
}

static void fill_circle(int cx, int cy, int radius, Uint32 color)
{
    int y;

    for (y = -radius; y <= radius; y++) {
        int x;
        for (x = -radius; x <= radius; x++) {
            if (x * x + y * y <= radius * radius) {
                put_pixel(cx + x, cy + y, color);
            }
        }
    }
}

static void fill_round_rect(int x, int y, int w, int h, int radius, Uint32 color)
{
    if (radius <= 0) {
        fill_rect(x, y, w, h, color);
        return;
    }
    if (radius * 2 > w) {
        radius = w / 2;
    }
    if (radius * 2 > h) {
        radius = h / 2;
    }

    fill_rect(x + radius, y, w - radius * 2, h, color);
    fill_rect(x, y + radius, w, h - radius * 2, color);
    fill_circle(x + radius, y + radius, radius, color);
    fill_circle(x + w - radius - 1, y + radius, radius, color);
    fill_circle(x + radius, y + h - radius - 1, radius, color);
    fill_circle(x + w - radius - 1, y + h - radius - 1, radius, color);
}

static void put_pixel(int x, int y, Uint32 color)
{
    Uint8 *p;

    if (x < 0 || y < 0 || x >= screen->w || y >= screen->h) {
        return;
    }

    p = (Uint8 *)screen->pixels + y * screen->pitch + x * screen->format->BytesPerPixel;
    switch (screen->format->BytesPerPixel) {
    case 1:
        *p = (Uint8)color;
        break;
    case 2:
        *(Uint16 *)p = (Uint16)color;
        break;
    case 3:
        if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
            p[0] = (color >> 16) & 0xff;
            p[1] = (color >> 8) & 0xff;
            p[2] = color & 0xff;
        } else {
            p[0] = color & 0xff;
            p[1] = (color >> 8) & 0xff;
            p[2] = (color >> 16) & 0xff;
        }
        break;
    default:
        *(Uint32 *)p = color;
        break;
    }
}

static unsigned char glyph5(char c, int row)
{
    static const unsigned char digits[10][7] = {
        {14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
        {14, 17, 1, 2, 4, 8, 31},    {30, 1, 1, 14, 1, 1, 30},
        {2, 6, 10, 18, 31, 2, 2},    {31, 16, 30, 1, 1, 17, 14},
        {6, 8, 16, 30, 17, 17, 14},  {31, 1, 2, 4, 8, 8, 8},
        {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 2, 12}
    };
    static const unsigned char letters[26][7] = {
        {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
        {14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30},
        {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
        {14, 17, 16, 23, 17, 17, 14}, {17, 17, 17, 31, 17, 17, 17},
        {14, 4, 4, 4, 4, 4, 14},      {7, 2, 2, 2, 18, 18, 12},
        {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
        {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17},
        {14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16},
        {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
        {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},
        {17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},
        {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
        {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31}
    };

    if (row <= 0 || row >= 8) {
        return 0;
    }
    row--;

    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    if (c >= 'A' && c <= 'Z') {
        return letters[c - 'A'][row];
    }
    if (c >= '0' && c <= '9') {
        return digits[c - '0'][row];
    }

    switch (c) {
    case '.':
        return row == 6 ? 4 : 0;
    case '-':
        return row == 3 ? 31 : 0;
    case '_':
        return row == 6 ? 31 : 0;
    case '/': {
        /* Bit 4 is the leftmost column: rise from bottom-left to top-right. */
        static const unsigned char slash[7] = { 1, 1, 2, 4, 8, 16, 16 };
        return row >= 0 && row < 7 ? slash[row] : 0;
    }
    case ':':
        return (row == 2 || row == 5) ? 4 : 0;
    case '+':
        return row == 3 ? 14 : (row == 2 || row == 4 ? 4 : 0);
    case '(':
        return row == 0 ? 2 : (row == 6 ? 2 : 4);
    case ')':
        return row == 0 ? 8 : (row == 6 ? 8 : 4);
    case '&':
        return row == 3 ? 10 : (row == 6 ? 13 : 4);
    default:
        return 0;
    }
}

static unsigned char glyph_row(char c, int row)
{
    return (unsigned char)(glyph5(c, row) << 1);
}

static void draw_char(int x, int y, char c, Uint32 fg)
{
    int row;
    int col;

    for (row = 0; row < CHAR_H; row++) {
        unsigned char bits = glyph_row(c, row);
        for (col = 0; col < CHAR_W; col++) {
            if (bits & (1 << (7 - col))) {
                put_pixel(x + col, y + row, fg);
            }
        }
    }
}

/* --- CJK / fullwidth text support -------------------------------------- */
/* Rendering model: a string that contains ANY non-ASCII byte is drawn in
 * "wide" mode where every glyph occupies a 16x16 cell (ASCII glyphs are
 * scaled 2x to match). Pure-ASCII strings keep the legacy 8x8 path so the
 * existing UI is pixel-identical. */

static int text_has_wide(const char *s)
{
    if (!s) {
        return 0;
    }
    while (*s) {
        if ((unsigned char)*s >= 0x80) {
            return 1;
        }
        s++;
    }
    return 0;
}

/* Codepoint pixel width in wide mode. CJK glyphs are native 16px; ASCII is
 * sized to match the surrounding pure-ASCII text at the same scale so that a
 * row containing Japanese keeps Latin characters looking like its neighbours
 * (8px at scale 1 in list rows, 16px at scale 2 in the now-playing title). */
static int cp_px_wide(uint32_t cp, int scale)
{
    if (cp < 0x80) {
        return CHAR_W * scale;
    }
    return 16;
}

/* Full visual pixel width of a string (for centering/right-align). */
static int text_px_w(const char *s, int scale)
{
    const char *p;
    int w;

    if (!s) {
        return 0;
    }
    if (!text_has_wide(s)) {
        return (int)strlen(s) * CHAR_W * scale;
    }
    p = s;
    w = 0;
    while (*p) {
        uint32_t cp = utf8_decode(&p);
        if (cp == 0 || cp == (uint32_t)-1) {
            break;
        }
        w += cp_px_wide(cp, scale);
    }
    return w;
}

/* Draw one 16x16 CJK glyph; missing glyphs render as an outline box. */
static void draw_cjk_glyph(int x, int y, uint32_t cp, Uint32 fg)
{
    int idx = font_cjk_find(cp);
    int row;
    int col;

    if (idx < 0) {
        /* tofu box so missing glyphs are visible instead of invisible */
        for (row = 0; row < 16; row++) {
            for (col = 0; col < 16; col++) {
                if (row == 0 || row == 15 || col == 0 || col == 15) {
                    put_pixel(x + col, y + row, fg);
                }
            }
        }
        return;
    }
    {
        const unsigned char *g = cjk_bits + idx * 32;
        for (row = 0; row < 16; row++) {
            unsigned char b0 = g[row * 2];
            unsigned char b1 = g[row * 2 + 1];
            for (col = 0; col < 8; col++) {
                if (b0 & (0x80 >> col)) {
                    put_pixel(x + col, y + row, fg);
                }
            }
            for (col = 0; col < 8; col++) {
                if (b1 & (0x80 >> col)) {
                    put_pixel(x + 8 + col, y + row, fg);
                }
            }
        }
    }
}

static void draw_char_scaled(int x, int y, char c, Uint32 fg, int scale)
{
    int row;
    int col;

    if (scale <= 1) {
        draw_char(x, y, c, fg);
        return;
    }

    for (row = 0; row < CHAR_H; row++) {
        unsigned char bits = glyph_row(c, row);
        for (col = 0; col < CHAR_W; col++) {
            if (bits & (1 << (7 - col))) {
                int sy;
                int sx;
                for (sy = 0; sy < scale; sy++) {
                    for (sx = 0; sx < scale; sx++) {
                        put_pixel(x + col * scale + sx, y + row * scale + sy, fg);
                    }
                }
            }
        }
    }
}

/* Draw one codepoint in wide mode at (x,y) cell origin. ASCII glyphs are
 * rendered at the native legacy size for the given scale (8px at scale 1, so
 * a row containing Japanese keeps its Latin characters looking exactly like
 * pure-ASCII neighbour rows; 16px at scale 2, matching scaled titles).
 * CJK glyphs are 16x16; at scale 1 they are shifted up 4px so they stay
 * vertically centered in the 17px row slot whose selection highlight starts
 * 5px ABOVE the row top (without the shift the bottom 5px would clip under
 * the highlight of the row below). At scale 2 (now-playing title) there is
 * no tight row slot, so CJK sits on the same top edge as the 16px ASCII. */
static void draw_wide_cp(int x, int y, uint32_t cp, Uint32 fg, int scale)
{
    if (cp < 0x80) {
        if (cp >= 0x20 && cp != 0x7f) {
            if (scale <= 1) {
                draw_char(x, y, (char)cp, fg);
            } else {
                draw_char_scaled(x, y, (char)cp, fg, scale);
            }
        }
    } else {
        if (scale <= 1) {
            y -= 4;
        }
        draw_cjk_glyph(x, y, cp, fg);
    }
}

/* Wide-mode text draw: max_px is the pixel budget; returns px consumed. */
static int draw_text_wide_px(int x, int y, const char *text, Uint32 fg, int max_px, int scale)
{
    const char *p = text;
    int consumed = 0;

    while (*p) {
        uint32_t cp = utf8_decode(&p);
        int w;
        if (cp == 0 || cp == (uint32_t)-1) {
            break;
        }
        w = cp_px_wide(cp, scale);
        if (max_px > 0 && consumed + w > max_px) {
            break;
        }
        draw_wide_cp(x + consumed, y, cp, fg, scale);
        consumed += w;
    }
    return consumed;
}

static void draw_text(int x, int y, const char *text, Uint32 fg, int max_chars)
{
    int i;

    if (!text) {
        return;
    }

    if (!text_has_wide(text)) {
        for (i = 0; text[i] && (max_chars <= 0 || i < max_chars); i++) {
            draw_char(x + i * CHAR_W, y, text[i], fg);
        }
        return;
    }

    draw_text_wide_px(x, y, text, fg, max_chars > 0 ? max_chars * CHAR_W : 0, 1);
}

static void draw_text_right(int right_x, int y, const char *text, Uint32 fg, int max_chars)
{
    int len;
    int w;

    if (!text) {
        return;
    }

    if (!text_has_wide(text)) {
        len = (int)strlen(text);
        if (max_chars > 0 && len > max_chars) {
            len = max_chars;
        }
        draw_text(right_x - len * CHAR_W, y, text, fg, max_chars);
        return;
    }

    w = text_px_w(text, 1);
    if (max_chars > 0 && w > max_chars * CHAR_W) {
        w = max_chars * CHAR_W;
    }
    draw_text_wide_px(right_x - w, y, text, fg, max_chars > 0 ? max_chars * CHAR_W : 0, 1);
}

static void draw_text_scaled(int x, int y, const char *text, Uint32 fg, int max_chars, int scale)
{
    int i;

    if (!text) {
        return;
    }

    if (!text_has_wide(text)) {
        for (i = 0; text[i] && (max_chars <= 0 || i < max_chars); i++) {
            draw_char_scaled(x + i * CHAR_W * scale, y, text[i], fg, scale);
        }
        return;
    }

    /* Wide strings at scale 2 (now-playing title): ASCII is drawn at scale 2
     * (== CHAR_W*2) and CJK glyphs are native 16x16, so the whole title is
     * uniformly 16px tall. */
    draw_text_wide_px(x, y, text, fg, max_chars > 0 ? max_chars * CHAR_W * scale : 0, scale);
}

static void draw_marquee_text(int x, int y, const char *text, Uint32 fg, int max_chars, int active)
{
    int len;

    if (!text || max_chars <= 0) {
        return;
    }

    if (!text_has_wide(text)) {
        len = (int)strlen(text);
        if (!active || len <= max_chars) {
            draw_text(x, y, text, fg, max_chars);
            return;
        }

        {
            int offset = 0;
            char window[96];
            offset = (int)((SDL_GetTicks() / 220) % (Uint32)(len + 4));
            if (offset >= len) {
                offset = 0;
            }
            snprintf(window, sizeof(window), "%s    %s", text + offset, text);
            draw_text(x, y, window, fg, max_chars);
        }
        return;
    }

    /* Wide-mode marquee: scroll by whole codepoints, keep UTF-8 intact. */
    {
        uint32_t cps[320];
        int n = 0;
        const char *p = text;
        int full_px = text_px_w(text, 1);
        int budget_px = max_chars * CHAR_W;
        int step;

        while (*p && n < (int)(sizeof(cps) / sizeof(cps[0]))) {
            uint32_t cp = utf8_decode(&p);
            if (cp == 0 || cp == (uint32_t)-1) {
                break;
            }
            cps[n++] = cp;
        }

        if (!active || full_px <= budget_px) {
            draw_text_wide_px(x, y, text, fg, budget_px, 1);
            return;
        }

        /* scrolling window of whole codepoints plus a gap */
        step = (int)((SDL_GetTicks() / 220) % (Uint32)(n + 2));
        {
            int drawn = 0;
            int i = step;
            int loops = 0;
            while (drawn < budget_px && loops < 300) {
                uint32_t cp;
                int w;
                loops++;
                if (i < n) {
                    cp = cps[i];
                    w = cp_px_wide(cp, 1);
                    i++;
                } else if (i < n + 2) {
                    cp = ' ';
                    w = cp_px_wide(' ', 1);
                    i++;
                    if (drawn + w > budget_px) {
                        break;
                    }
                    draw_wide_cp(x + drawn, y, cp, fg, 1);
                    drawn += w;
                    continue;
                } else {
                    i = 0;
                    continue;
                }
                if (drawn + w > budget_px) {
                    break;
                }
                draw_wide_cp(x + drawn, y, cp, fg, 1);
                drawn += w;
            }
        }
    }
}

static void draw_fallback_background(void)
{
    int y;

    fill_rect(0, 0, SCREEN_W, SCREEN_H, rgb(34, 38, 43));
    for (y = 0; y < SCREEN_H; y += 24) {
        Uint8 shade = (Uint8)(48 + (y * 42) / SCREEN_H);
        fill_rect(0, y, SCREEN_W, 24, rgb(shade, (Uint8)(shade + 7), (Uint8)(shade + 12)));
    }
    fill_circle(92, 86, 76, rgb(62, 72, 82));
    fill_circle(566, 388, 98, rgb(52, 62, 71));
}

static void draw_equalizer_bg(AudioState state)
{
    static const unsigned char base[24] = {
        10, 22, 16, 34, 28, 42, 18, 30,
        12, 38, 24, 44, 14, 32, 20, 40,
        26, 18, 36, 16, 30, 22, 42, 12
    };
    int i;
    int frame = state == AUDIO_PLAYING ? (int)(SDL_GetTicks() / 95) : 0;
    Uint32 bar = rgb(25, 33, 40);
    Uint32 bar_hi = rgb(31, 43, 52);

    for (i = 0; i < 24; i++) {
        int x = 42 + i * 15;
        int h = base[(i + frame) % 24] + ((i * 7 + frame * 3) % 11);
        fill_round_rect(x, 340 - h, 7, h, 3, i % 4 == 0 ? bar_hi : bar);
    }

    for (i = 0; i < 33; i++) {
        int x = 238 + i * 11;
        int h = 8 + ((base[(i * 3 + frame) % 24] + frame) % 30);
        fill_round_rect(x, 92 - h, 5, h, 2, bar);
    }
}

/* ---- 16bpp alpha helpers (screen is always 565, see ui_init) ---- */

static Uint16 pack565(int r, int g, int b)
{
    return (Uint16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void blend_px(int x, int y, int r, int g, int b, int a)
{
    Uint16 *p;
    int pr;
    int pg;
    int pb;

    if (a <= 0 || x < 0 || y < 0 || x >= screen->w || y >= screen->h) {
        return;
    }
    p = (Uint16 *)((Uint8 *)screen->pixels + y * screen->pitch) + x;
    if (a >= 255) {
        *p = pack565(r, g, b);
        return;
    }
    pr = (*p >> 11) & 0x1f;
    pg = (*p >> 5) & 0x3f;
    pb = *p & 0x1f;
    pr = (pr << 3) | (pr >> 2);
    pg = (pg << 2) | (pg >> 4);
    pb = (pb << 3) | (pb >> 2);
    pr += ((r - pr) * a) >> 8;
    pg += ((g - pg) * a) >> 8;
    pb += ((b - pb) * a) >> 8;
    *p = pack565(pr, pg, pb);
}

static int mix_ch(int a, int b, float t)
{
    return a + (int)((float)(b - a) * t);
}

/* Glow/tint color for the now-playing view, derived from the cover art
 * (falls back to the accent orange). Updated by cover_sync(). */
static int tint_rgb[3] = { 200, 134, 20 };

/* NOW PLAYING spectrum: big segmented bars drawn right after the dot grid, so
 * cover, title, timer and progress bar all render on top of it. No real FFT
 * is available from mpg123 -R, so levels are synthesized (bass-heavy envelope,
 * beat pulse, per-bar wobble) and smoothed with fast attack / gravity decay,
 * plus falling peak caps. Paused/stopped: bars sink to a low idle breathe. */
#define SPEC_BARS 46
#define SPEC_SEG_PITCH 4
#define SPEC_SEG_H 3
#define SPEC_SRC_BANDS 24

static UiSpectrumSource spec_source = NULL;
static int spec_latency_ms = 0;
static int play_raw_ms = -1;
static int play_base_ms = 0;
static Uint32 play_base_tick = 0;

void ui_set_spectrum_source(UiSpectrumSource source, int latency_ms)
{
    spec_source = source;
    spec_latency_ms = latency_ms;
}

void ui_set_playback_ms(int ms)
{
    if (ms != play_raw_ms) {
        play_raw_ms = ms;
        play_base_ms = ms;
        play_base_tick = SDL_GetTicks();
    }
}

/* mpg123 reports position per decoded frame; between reports, advance with
 * the wall clock (capped) so sampled levels move smoothly. */
static int playback_ms_now(int playing)
{
    int ms = play_base_ms;
    if (playing) {
        Uint32 d = SDL_GetTicks() - play_base_tick;
        ms += d > 250 ? 250 : (int)d;
    }
    return ms;
}

static void draw_spectrum_bg(AudioState state, int x, int base_y, int w, int max_h)
{
    static float level[SPEC_BARS];
    static float peak[SPEC_BARS];
    static float peak_vel[SPEC_BARS];
    static Uint32 last_tick = 0;
    static int inited = 0;
    Uint32 now = SDL_GetTicks();
    float t = (float)now / 1000.0f;
    float dt = inited ? (float)(now - last_tick) / 1000.0f : 0.033f;
    int playing = state == AUDIO_PLAYING;
    int step = w / SPEC_BARS;
    int bw = step - 4;
    int x0 = x + (w - SPEC_BARS * step + 4) / 2;
    int max_segs = max_h / SPEC_SEG_PITCH;
    float beat = fmodf(t, 0.48f) / 0.48f;
    float kick = expf(-beat * 7.0f);
    static const int low[3] = { 25, 31, 38 };
    static const int top_base[3] = { 44, 54, 64 };
    static const int cap_base[3] = { 70, 82, 94 };
    static const int bg[3] = { 18, 22, 27 };
    int top[3];
    int cap[3];
    unsigned char bands[SPEC_SRC_BANDS];
    int real = 0;
    int i;

    if (playing && spec_source) {
        int ms = playback_ms_now(1) - spec_latency_ms;
        real = spec_source(ms < 0 ? 0 : ms, bands, SPEC_SRC_BANDS);
    }

    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    last_tick = now;

    for (i = 0; i < 3; i++) {
        top[i] = mix_ch(top_base[i], tint_rgb[i] / 4, 0.4f);
        cap[i] = mix_ch(cap_base[i], tint_rgb[i] / 2, 0.5f);
    }

    for (i = 0; i < SPEC_BARS; i++) {
        float fi = (float)i / (float)(SPEC_BARS - 1);
        float target;
        float edge;
        int segs;
        int peak_seg;
        int bx = x0 + i * step;
        int s;

        if (real) {
            /* Map 24 analysis bands across the bars, linearly interpolated. */
            float p = fi * (float)(SPEC_SRC_BANDS - 1);
            int b = (int)p;
            float f = p - (float)b;
            float v = b + 1 < SPEC_SRC_BANDS
                ? (float)bands[b] * (1.0f - f) + (float)bands[b + 1] * f
                : (float)bands[b];
            target = v / 255.0f;
        } else if (playing) {
            float env = 0.92f - 0.5f * fi + 0.18f * sinf(fi * 3.14159f);
            float n = 0.5f
                + 0.24f * sinf(t * (3.1f + (float)i * 0.37f) + (float)i * 1.7f)
                + 0.16f * sinf(t * (7.3f + (float)(i % 7) * 0.9f) + (float)i * 0.6f)
                + 0.10f * sinf(t * 13.0f + (float)i * 2.3f);
            float lowness = 1.0f - fi;
            target = env * (0.2f + 0.62f * n) + 0.42f * kick * lowness * lowness;
        } else {
            target = 0.04f + 0.025f * sinf(t * 1.6f + (float)i * 0.45f);
        }
        if (target < 0.0f) {
            target = 0.0f;
        }
        if (target > 1.0f) {
            target = 1.0f;
        }

        if (!inited) {
            level[i] = target;
            peak[i] = target + 0.08f;
            peak_vel[i] = 0.0f;
        } else if (target > level[i]) {
            float k = dt * (real ? 30.0f : 18.0f);
            level[i] += (target - level[i]) * (k > 1.0f ? 1.0f : k);
        } else {
            float fall = dt * (real ? 2.2f : 1.4f);
            level[i] -= (level[i] - target) < fall ? (level[i] - target) : fall;
        }

        if (level[i] >= peak[i]) {
            peak[i] = level[i];
            peak_vel[i] = 0.0f;
        } else {
            peak_vel[i] += dt * 2.2f;
            peak[i] -= peak_vel[i] * dt;
            if (peak[i] < level[i]) {
                peak[i] = level[i];
            }
        }

        /* Soft horizontal vignette so the field melts into the shell. */
        edge = (float)(i < SPEC_BARS - 1 - i ? i : SPEC_BARS - 1 - i) / 6.0f;
        if (edge > 1.0f) {
            edge = 1.0f;
        }
        edge = 0.3f + 0.7f * edge;

        segs = (int)(level[i] * (float)max_segs + 0.5f);
        if (segs < 1) {
            segs = 1;
        }
        for (s = 0; s < segs; s++) {
            float fr = (float)s / (float)max_segs;
            int r = mix_ch(bg[0], mix_ch(low[0], top[0], fr), edge);
            int g = mix_ch(bg[1], mix_ch(low[1], top[1], fr), edge);
            int b = mix_ch(bg[2], mix_ch(low[2], top[2], fr), edge);
            fill_rect(bx, base_y - (s + 1) * SPEC_SEG_PITCH, bw, SPEC_SEG_H, rgb((Uint8)r, (Uint8)g, (Uint8)b));
        }

        peak_seg = (int)(peak[i] * (float)max_segs + 0.5f);
        if (peak_seg <= segs) {
            peak_seg = segs + 1;
        }
        if (peak_seg <= max_segs + 1) {
            int r = mix_ch(bg[0], cap[0], edge);
            int g = mix_ch(bg[1], cap[1], edge);
            int b = mix_ch(bg[2], cap[2], edge);
            fill_rect(bx, base_y - peak_seg * SPEC_SEG_PITCH + 1, bw, 2, rgb((Uint8)r, (Uint8)g, (Uint8)b));
        }
    }
    inited = 1;
}

/* Single diagonal gradient dot matrix for the now-playing background: dots
 * fade once, softly, from the equalizer bar color (top-left) to a muted
 * lighter blue-gray (bottom-right). No repeating bands; drawn early. */
static void draw_dot_grid(void)
{
    int x;
    int y;
    const float maxv = (float)(SCREEN_W - 60 + SCREEN_H - 52);

    for (y = 30; y < SCREEN_H - 22; y += 12) {
        for (x = 30; x < SCREEN_W - 22; x += 12) {
            float v = (float)((x - 30) + (y - 30)) / maxv;
            int r;
            int g;
            int b;
            if (v > 1.0f) {
                v = 1.0f;
            }
            r = 25 + (int)(v * 35.0f);
            g = 33 + (int)(v * 41.0f);
            b = 40 + (int)(v * 48.0f);
            fill_rect(x, y, 2, 2, rgb(r, g, b));
        }
    }
}

static void format_time_pair(int elapsed_seconds, int duration_seconds, char *out, size_t out_size)
{
    int elapsed_minutes;
    int duration_minutes;

    if (!out || out_size == 0) {
        return;
    }

    if (elapsed_seconds < 0) {
        elapsed_seconds = 0;
    }

    elapsed_minutes = elapsed_seconds / 60;
    elapsed_seconds %= 60;

    if (duration_seconds <= 0) {
        snprintf(out, out_size, "%02d:%02d / --:--", elapsed_minutes, elapsed_seconds);
        return;
    }

    duration_minutes = duration_seconds / 60;
    duration_seconds %= 60;
    snprintf(out, out_size, "%02d:%02d / %02d:%02d", elapsed_minutes, elapsed_seconds, duration_minutes, duration_seconds);
}

/* Bilinear-scale an RGBA cover into a fitted (letterboxed) RGB565 buffer of
 * dst x dst (corners are masked at blit time). Returns malloc'd buffer
 * (dst*dst*2) or NULL. Runs once per track change, not per frame. */
static unsigned char *scale_cover_565(const unsigned char *rgba, int sw, int sh, int dst)
{
    unsigned char *out;
    int dw, dh, ox, oy, i, j;
    float scale;
    Uint16 bg565;
    int *x0t, *x1t, *y0t, *y1t;
    float *fxt, *fyt;

    if (!rgba || sw <= 0 || sh <= 0 || dst <= 0) {
        return NULL;
    }
    scale = (float)dst / (sw > sh ? sw : sh);
    dw = (int)(sw * scale);
    dh = (int)(sh * scale);
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    if (dw > dst) dw = dst;
    if (dh > dst) dh = dst;
    ox = (dst - dw) / 2;
    oy = (dst - dh) / 2;

    out = (unsigned char *)malloc((size_t)dst * dst * 2);
    x0t = (int *)malloc((size_t)dw * sizeof(int));
    x1t = (int *)malloc((size_t)dw * sizeof(int));
    fxt = (float *)malloc((size_t)dw * sizeof(float));
    y0t = (int *)malloc((size_t)dh * sizeof(int));
    y1t = (int *)malloc((size_t)dh * sizeof(int));
    fyt = (float *)malloc((size_t)dh * sizeof(float));
    if (!out || !x0t || !x1t || !fxt || !y0t || !y1t || !fyt) {
        free(out); free(x0t); free(x1t); free(fxt); free(y0t); free(y1t); free(fyt);
        return NULL;
    }

    for (i = 0; i < dw; i++) {
        float gx = (i + 0.5f) * sw / dw - 0.5f;
        int x0 = (int)gx;
        if (x0 < 0) x0 = 0;
        if (x0 >= sw) x0 = sw - 1;
        x0t[i] = x0;
        x1t[i] = x0 + 1 < sw ? x0 + 1 : x0;
        fxt[i] = gx - x0;
    }
    for (j = 0; j < dh; j++) {
        float gy = (j + 0.5f) * sh / dh - 0.5f;
        int y0 = (int)gy;
        if (y0 < 0) y0 = 0;
        if (y0 >= sh) y0 = sh - 1;
        y0t[j] = y0;
        y1t[j] = y0 + 1 < sh ? y0 + 1 : y0;
        fyt[j] = gy - y0;
    }

    bg565 = (Uint16)(((12 >> 3) << 11) | ((16 >> 2) << 5) | (21 >> 3));

    for (j = 0; j < dst; j++) {
        for (i = 0; i < dst; i++) {
            int px = i - ox;
            int py = j - oy;
            Uint16 px565;
            if (px < 0 || px >= dw || py < 0 || py >= dh) {
                px565 = bg565;
            } else {
                const unsigned char *p00 = rgba + ((size_t)y0t[py] * sw + x0t[px]) * 4;
                const unsigned char *p10 = rgba + ((size_t)y0t[py] * sw + x1t[px]) * 4;
                const unsigned char *p01 = rgba + ((size_t)y1t[py] * sw + x0t[px]) * 4;
                const unsigned char *p11 = rgba + ((size_t)y1t[py] * sw + x1t[px]) * 4;
                float fx = fxt[px];
                float fy = fyt[py];
                int r, g, b;
                float top, bot;
                top = p00[0] * (1.0f - fx) + p10[0] * fx;
                bot = p01[0] * (1.0f - fx) + p11[0] * fx;
                r = (int)(top * (1.0f - fy) + bot * fy + 0.5f);
                top = p00[1] * (1.0f - fx) + p10[1] * fx;
                bot = p01[1] * (1.0f - fx) + p11[1] * fx;
                g = (int)(top * (1.0f - fy) + bot * fy + 0.5f);
                top = p00[2] * (1.0f - fx) + p10[2] * fx;
                bot = p01[2] * (1.0f - fx) + p11[2] * fx;
                b = (int)(top * (1.0f - fy) + bot * fy + 0.5f);
                if (r < 0) r = 0;
                if (r > 255) r = 255;
                if (g < 0) g = 0;
                if (g > 255) g = 255;
                if (b < 0) b = 0;
                if (b > 255) b = 255;
                px565 = (Uint16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
            out[((size_t)j * dst + i) * 2] = (unsigned char)(px565 & 0xff);
            out[((size_t)j * dst + i) * 2 + 1] = (unsigned char)(px565 >> 8);
        }
    }

    free(x0t); free(x1t); free(fxt); free(y0t); free(y1t); free(fyt);
    return out;
}

/* ---- NOW PLAYING cover: borderless rounded card with anti-aliased corners,
 * soft drop shadow, ambient glow tinted from the cover, and a faint 1px rim.
 * Masks are computed once; art is decoded/scaled once per track change. ---- */
#define COVER_SIZE 160
#define COVER_RADIUS 16
#define HALO_PAD 36
#define HALO_SIZE (COVER_SIZE + HALO_PAD * 2)

static unsigned char *cover_mask = NULL;   /* corner coverage, COVER_SIZE^2 */
static unsigned char *cover_rim = NULL;    /* 1px edge highlight */
static unsigned char *halo_glow = NULL;    /* HALO_SIZE^2 */
static unsigned char *halo_shadow = NULL;
static unsigned char *cover_art_565 = NULL;
static unsigned char *cover_blank_565 = NULL;
static char cover_path[1024] = "";

/* Signed distance from pixel center (px, py) to a w x h rounded rect whose
 * top-left is the origin. Negative inside. */
static float sd_round_rect(float px, float py, float w, float h, float r)
{
    float qx = fabsf(px - w * 0.5f) - (w * 0.5f - r);
    float qy = fabsf(py - h * 0.5f) - (h * 0.5f - r);
    float ox = qx > 0.0f ? qx : 0.0f;
    float oy = qy > 0.0f ? qy : 0.0f;
    float inner = qx > qy ? qx : qy;

    if (inner > 0.0f) {
        inner = 0.0f;
    }
    return sqrtf(ox * ox + oy * oy) + inner - r;
}

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

static int cover_masks_init(void)
{
    int i;
    int j;

    if (cover_mask) {
        return 1;
    }
    cover_mask = (unsigned char *)malloc(COVER_SIZE * COVER_SIZE);
    cover_rim = (unsigned char *)malloc(COVER_SIZE * COVER_SIZE);
    halo_glow = (unsigned char *)malloc(HALO_SIZE * HALO_SIZE);
    halo_shadow = (unsigned char *)malloc(HALO_SIZE * HALO_SIZE);
    cover_blank_565 = (unsigned char *)malloc(COVER_SIZE * COVER_SIZE * 2);
    if (!cover_mask || !cover_rim || !halo_glow || !halo_shadow || !cover_blank_565) {
        free(cover_mask); free(cover_rim); free(halo_glow); free(halo_shadow); free(cover_blank_565);
        cover_mask = cover_rim = halo_glow = halo_shadow = cover_blank_565 = NULL;
        return 0;
    }

    for (j = 0; j < COVER_SIZE; j++) {
        for (i = 0; i < COVER_SIZE; i++) {
            float d = sd_round_rect(i + 0.5f, j + 0.5f, COVER_SIZE, COVER_SIZE, COVER_RADIUS);
            float rim = 1.0f - fabsf(d + 1.0f);
            float fy = (float)j / COVER_SIZE;
            Uint16 px;
            cover_mask[j * COVER_SIZE + i] = (unsigned char)(clamp01(0.5f - d) * 255.0f);
            /* Rim is brighter on top, fades toward the bottom like a light edge. */
            cover_rim[j * COVER_SIZE + i] = (unsigned char)(clamp01(rim) * (70.0f - 45.0f * fy));
            /* No-art card: soft diagonal slate gradient. */
            {
                float g = clamp01(((float)i * 0.35f + (float)j) / (COVER_SIZE * 1.35f));
                px = pack565(mix_ch(54, 20, g), mix_ch(64, 25, g), mix_ch(76, 32, g));
            }
            cover_blank_565[(j * COVER_SIZE + i) * 2] = (unsigned char)(px & 0xff);
            cover_blank_565[(j * COVER_SIZE + i) * 2 + 1] = (unsigned char)(px >> 8);
        }
    }

    for (j = 0; j < HALO_SIZE; j++) {
        for (i = 0; i < HALO_SIZE; i++) {
            float px = i + 0.5f - HALO_PAD;
            float py = j + 0.5f - HALO_PAD;
            float dg = sd_round_rect(px, py, COVER_SIZE, COVER_SIZE, COVER_RADIUS);
            float ds = sd_round_rect(px, py - 8.0f, COVER_SIZE, COVER_SIZE, COVER_RADIUS);
            float g = 1.0f - clamp01(dg / (float)HALO_PAD);
            float s = 1.0f - clamp01((ds + 6.0f) / 22.0f);
            halo_glow[j * HALO_SIZE + i] = (unsigned char)(g * g * g * 120.0f);
            halo_shadow[j * HALO_SIZE + i] = (unsigned char)(s * s * 200.0f);
        }
    }
    return 1;
}

/* Reload art only when the active track path changes; derive the glow tint
 * from the art's average color, pushed toward a vivid, light version. */
static void cover_sync(const char *art_path)
{
    unsigned char *rgba = NULL;
    int w = 0;
    int h = 0;

    if (!art_path) {
        art_path = "";
    }
    if (strcmp(cover_path, art_path) == 0) {
        return;
    }
    snprintf(cover_path, sizeof(cover_path), "%s", art_path);
    free(cover_art_565);
    cover_art_565 = NULL;
    tint_rgb[0] = 200;
    tint_rgb[1] = 134;
    tint_rgb[2] = 20;

    if (art_path[0] && album_art_load(art_path, &rgba, &w, &h)) {
        long sum[3] = { 0, 0, 0 };
        long n = 0;
        int sx = w > 64 ? w / 64 : 1;
        int sy = h > 64 ? h / 64 : 1;
        int i;
        int j;
        int mx;

        cover_art_565 = scale_cover_565(rgba, w, h, COVER_SIZE);
        for (j = 0; j < h; j += sy) {
            for (i = 0; i < w; i += sx) {
                const unsigned char *p = rgba + ((size_t)j * w + i) * 4;
                sum[0] += p[0];
                sum[1] += p[1];
                sum[2] += p[2];
                n++;
            }
        }
        free(rgba);
        if (n > 0) {
            int avg[3];
            int lum;
            for (i = 0; i < 3; i++) {
                avg[i] = (int)(sum[i] / n);
            }
            /* Boost saturation 1.6x around luma, then normalize brightness. */
            lum = (avg[0] * 3 + avg[1] * 6 + avg[2]) / 10;
            for (i = 0; i < 3; i++) {
                int v = lum + (avg[i] - lum) * 8 / 5;
                tint_rgb[i] = v < 0 ? 0 : (v > 255 ? 255 : v);
            }
            mx = tint_rgb[0] > tint_rgb[1] ? tint_rgb[0] : tint_rgb[1];
            mx = mx > tint_rgb[2] ? mx : tint_rgb[2];
            if (mx < 1) {
                mx = 1;
            }
            for (i = 0; i < 3; i++) {
                tint_rgb[i] = tint_rgb[i] * 225 / mx;
            }
        }
    }
}

static void draw_cover_halo(int x, int y)
{
    int i;
    int j;
    int ox = x - HALO_PAD;
    int oy = y - HALO_PAD;

    if (!cover_masks_init()) {
        return;
    }
    for (j = 0; j < HALO_SIZE; j++) {
        for (i = 0; i < HALO_SIZE; i++) {
            int g = halo_glow[j * HALO_SIZE + i];
            int s = halo_shadow[j * HALO_SIZE + i];
            if (g) {
                blend_px(ox + i, oy + j, tint_rgb[0], tint_rgb[1], tint_rgb[2], g);
            }
            if (s) {
                blend_px(ox + i, oy + j, 4, 6, 9, s);
            }
        }
    }
}

static void blit_cover_masked(int x, int y, const unsigned char *buf)
{
    int i;
    int j;

    for (j = 0; j < COVER_SIZE; j++) {
        Uint16 *row;
        if (y + j < 0 || y + j >= screen->h) {
            continue;
        }
        row = (Uint16 *)((Uint8 *)screen->pixels + (y + j) * screen->pitch);
        if (j >= COVER_RADIUS && j < COVER_SIZE - COVER_RADIUS && x >= 0 && x + COVER_SIZE <= screen->w) {
            memcpy(row + x, buf + (size_t)j * COVER_SIZE * 2, COVER_SIZE * 2);
            continue;
        }
        for (i = 0; i < COVER_SIZE; i++) {
            int a = cover_mask[j * COVER_SIZE + i];
            const unsigned char *p = buf + ((size_t)j * COVER_SIZE + i) * 2;
            Uint16 v = (Uint16)(p[0] | (p[1] << 8));
            int r;
            int g;
            int b;
            if (!a) {
                continue;
            }
            r = (v >> 11) & 0x1f;
            g = (v >> 5) & 0x3f;
            b = v & 0x1f;
            blend_px(x + i, y + j, (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), a);
        }
    }
}

static void draw_cover_rim(int x, int y)
{
    int i;
    int j;

    for (j = 0; j < COVER_SIZE; j++) {
        for (i = 0; i < COVER_SIZE; i++) {
            int a = cover_rim[j * COVER_SIZE + i];
            if (a) {
                blend_px(x + i, y + j, 255, 255, 255, a);
            }
        }
    }
}

static void draw_album_visual(int x, int y, int spinning)
{
    static const int marker_x[32] = {
        0, 8, 16, 24, 30, 36, 39, 41,
        42, 41, 39, 36, 30, 24, 16, 8,
        0, -8, -16, -24, -30, -36, -39, -41,
        -42, -41, -39, -36, -30, -24, -16, -8
    };
    static const int marker_y[32] = {
        -42, -41, -39, -36, -30, -24, -16, -8,
        0, 8, 16, 24, 30, 36, 39, 41,
        42, 41, 39, 36, 30, 24, 16, 8,
        0, -8, -16, -24, -30, -36, -39, -41
    };
    static int frame = 0;
    static Uint32 last_tick = 0;
    Uint32 now = SDL_GetTicks();
    int cx = x + COVER_SIZE / 2;
    int cy = y + COVER_SIZE / 2;
    Uint32 hole = rgb(24, 29, 36);
    Uint32 accent = rgb(255, 170, 0);
    Uint32 disc = rgb(229, 236, 241);
    Uint32 ring = rgb(202, 212, 220);
    Uint32 shine = rgb(246, 249, 251);

    if (!cover_masks_init()) {
        return;
    }
    if (spinning) {
        if (last_tick == 0) {
            last_tick = now;
        }
        while (now - last_tick >= 45) {
            frame = (frame + 1) & 31;
            last_tick += 45;
        }
    } else {
        last_tick = now;
    }

    blit_cover_masked(x, y, cover_art_565 ? cover_art_565 : cover_blank_565);
    if (!cover_art_565) {
        fill_circle(cx + 3, cy + 4, 46, rgb(14, 18, 23));
        fill_circle(cx, cy, 46, disc);
        fill_circle(cx, cy, 34, ring);
        fill_circle(cx, cy, 23, disc);
        fill_circle(cx, cy, 12, hole);
        fill_circle(cx, cy, 5, accent);
        fill_circle(cx + marker_x[frame] / 2, cy + marker_y[frame] / 2, 6, shine);
        fill_circle(cx + marker_x[frame], cy + marker_y[frame], 4, hole);
        fill_circle(cx + marker_x[frame], cy + marker_y[frame], 2, accent);
    }
    draw_cover_rim(x, y);
}

/* Frosted info card: rounded rect blended over whatever is behind it (no hard
 * border), anti-aliased corners, a faint top rim light and a darker bottom lip. */
static void draw_glass_panel(int x, int y, int w, int h, int r)
{
    int i;
    int j;

    for (j = 0; j < h; j++) {
        int corner_row = j < r || j >= h - r;
        for (i = 0; i < w; i++) {
            int a = 255;
            int edge = j == 0 || j == h - 1;
            if (corner_row && (i < r || i >= w - r)) {
                float d = sd_round_rect(i + 0.5f, j + 0.5f, (float)w, (float)h, (float)r);
                float cov = clamp01(0.5f - d);
                if (cov <= 0.0f) {
                    continue;
                }
                a = (int)(cov * 255.0f);
                edge = d > -1.2f;
            }
            blend_px(x + i, y + j, 30, 37, 45, a * 220 / 255);
            if (edge && j < h / 2) {
                blend_px(x + i, y + j, 255, 255, 255, 46 * a / 255);
            } else if (edge) {
                blend_px(x + i, y + j, 0, 0, 0, 60 * a / 255);
            } else if (j == 1) {
                blend_px(x + i, y + j, 255, 255, 255, 12);
            }
        }
    }
}

/* Small 9x9 transport glyph next to the status text. */
static void draw_status_icon(int x, int y, AudioState state, Uint32 color)
{
    int k;

    if (state == AUDIO_PLAYING) {
        for (k = 0; k < 5; k++) {
            fill_rect(x + k * 2, y + k, 2, 9 - k * 2, color);
        }
    } else if (state == AUDIO_PAUSED) {
        fill_rect(x + 1, y, 3, 9, color);
        fill_rect(x + 6, y, 3, 9, color);
    } else {
        fill_round_rect(x + 1, y + 1, 7, 7, 1, color);
    }
}

/* Volume as a slim rising wedge (triangle ramp): thin on the left, taller on
 * the right, so the shape itself reads as "louder". Filled part in accent,
 * remainder as a dim track; the sloped top edge is anti-aliased. */
static void draw_volume_wedge(int x, int y, int w, int h, int volume, Uint32 fg, Uint32 muted)
{
    char num[8];
    int fill_w;
    int pct;
    int i;
    int k;

    if (volume < 0) {
        volume = 0;
    }
    if (volume > UI_VOLUME_MAX) {
        volume = UI_VOLUME_MAX;
    }
    /* No '%' glyph in the bitmap font; a bare 0-100 number reads as percent. */
    pct = (volume * 100 + UI_VOLUME_MAX / 2) / UI_VOLUME_MAX;
    snprintf(num, sizeof(num), "%d", pct);

    /* Speaker glyph + label, value right-aligned to the wedge's end. */
    fill_rect(x, y + 2, 3, 4, muted);
    for (k = 0; k < 4; k++) {
        fill_rect(x + 3 + k, y + 2 - k, 1, 4 + k * 2, muted);
    }
    draw_text(x + 12, y, "VOL", muted, 3);
    draw_text_right(x + w, y, num, fg, 3);

    fill_w = (w * volume) / UI_VOLUME_MAX;
    for (i = 0; i < w; i++) {
        float top = (float)h - (2.0f + (float)(h - 2) * (float)i / (float)(w - 1));
        int full = (int)top;
        int frac = (int)((1.0f - (top - (float)full)) * 255.0f);
        int base_y = y + 18;
        int on = i < fill_w;
        int r = on ? 255 : 52;
        int g = on ? 170 : 62;
        int b = on ? 0 : 72;
        /* Fade the leading edge of the fill for a soft "head". */
        if (on && i >= fill_w - 3) {
            r = mix_ch(r, 255, 0.5f);
            g = mix_ch(g, 214, 0.5f);
            b = mix_ch(b, 120, 0.5f);
        }
        fill_rect(x + i, base_y + full + 1, 1, h - full - 1, rgb((Uint8)r, (Uint8)g, (Uint8)b));
        blend_px(x + i, base_y + full, r, g, b, frac);
    }
}

static void draw_progress_bar(int x, int y, int w, int h, int elapsed, int duration, Uint32 muted, Uint32 hi)
{
    int fill_w = 0;

    if (elapsed < 0) {
        elapsed = 0;
    }
    if (duration > 0) {
        if (elapsed > duration) {
            elapsed = duration;
        }
        fill_w = (w * elapsed) / duration;
    }

    fill_round_rect(x, y, w, h, 3, muted);
    if (fill_w > 0) {
        fill_round_rect(x, y, fill_w, h, 3, hi);
    }
}

static void format_bitrate_label(int bitrate_kbps, int vbr, int lossless_bits, int sample_rate, char *out, size_t out_size)
{
    if (lossless_bits > 0 && sample_rate > 0) {
        /* Lossless: bit depth / kHz, e.g. 16/44, 24/96, 24/192. */
        snprintf(out, out_size, "%d/%d", lossless_bits, sample_rate / 1000);
        return;
    }
    if (bitrate_kbps <= 0) {
        out[0] = '\0';
        return;
    }
    if (vbr) {
        snprintf(out, out_size, "VBR");
    } else {
        snprintf(out, out_size, "%dK", bitrate_kbps);
    }
}

static void format_clock_label(char *out, size_t out_size)
{
    time_t now;
    struct tm *local_tm;

    now = time(NULL);
    local_tm = localtime(&now);
    if (!local_tm) {
        out[0] = '\0';
        return;
    }
    snprintf(out, out_size, "%02d:%02d", local_tm->tm_hour, local_tm->tm_min);
}

static const char *state_label(AudioState state)
{
    switch (state) {
    case AUDIO_PLAYING:
        return "Playing";
    case AUDIO_PAUSED:
        return "Paused";
    default:
        return "Stopped";
    }
}

int ui_init(void)
{
    screen = SDL_SetVideoMode(SCREEN_W, SCREEN_H, 16, SDL_SWSURFACE);
    if (!screen) {
        fprintf(stderr, "SDL_SetVideoMode failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_ShowCursor(SDL_DISABLE);
    SDL_WM_SetCaption("Garlic MP3", "Garlic MP3");
    background = SDL_LoadBMP("assets/background.bmp");
    if (!background) {
        background = SDL_LoadBMP("background.bmp");
    }
    return 0;
}

void ui_shutdown(void)
{
    if (background) {
        SDL_FreeSurface(background);
        background = NULL;
    }
    screen = NULL;
}

void ui_render(const TrackList *list, int selected, int playing, AudioState state, int elapsed_seconds, int duration_seconds, int volume, const char *repeat_label, const char *eq_label, int favorites_only, const char *message, int view_mode)
{
    int i;
    int first = 0;
    int visible = 12;
    Uint32 shell;
    Uint32 screen_bg;
    Uint32 border;
    Uint32 fg;
    Uint32 muted;
    Uint32 hi;
    Uint32 hi_text;
    Uint32 panel_shadow;
    const char *now_title = NULL;

    if (!screen) {
        return;
    }

    shell = rgb(18, 22, 27);
    screen_bg = rgb(30, 36, 43);
    border = rgb(76, 88, 100);
    fg = rgb(235, 241, 246);
    muted = rgb(151, 163, 174);
    hi = rgb(255, 170, 0);
    hi_text = rgb(22, 24, 32);
    panel_shadow = rgb(7, 10, 13);

    if (background) {
        SDL_BlitSurface(background, NULL, screen, NULL);
    }
    SDL_LockSurface(screen);
    if (!background) {
        draw_fallback_background();
    }

    fill_round_rect(18, 14, SCREEN_W - 36, SCREEN_H - 28, 18, panel_shadow);
    fill_round_rect(22, 18, SCREEN_W - 44, SCREEN_H - 36, 16, shell);
    /* EQ background header cuma di mode list; now playing pakai strip EQ sendiri */
    if (view_mode == 0) {
        draw_equalizer_bg(state);
    }

    /* Header stacked: judul di atas, info bar di bawah — margin kiri sama (x=38) */
    draw_text_scaled(38, 52, "Garlic MP3", fg, 10, 2);
    draw_text_right(594, 49, state_label(state), muted, 12);
    draw_text(38, 74, repeat_label ? repeat_label : "Repeat All", muted, 14);
    if (favorites_only) {
        draw_text(158, 74, "Favorites", hi, 12);
    }
    /* Label EQ dipindah: di now playing view (metadata line kanan), bukan header */

    if (list->count == 0) {
        fill_round_rect(42, 108, 556, 252, 14, panel_shadow);
        fill_round_rect(38, 104, 556, 252, 14, border);
        fill_round_rect(40, 106, 552, 248, 12, screen_bg);
        draw_text_scaled(72, 152, "No MP3 files", fg, 12, 2);
        draw_text(74, 192, "Use Roms/MUSIC or app MUSIC folder", muted, 58);
    } else if (view_mode == 1) {
        /* NOW PLAYING — full width */
        char counter[32];
        int duration;

        draw_dot_grid();
        {
            const char *art_path = (playing >= 0 && playing < list->count)
                ? list->tracks[playing].path : NULL;
            cover_sync(art_path);
        }
        /* Spectrum first: everything else in this view draws on top of it. */
        draw_spectrum_bg(state, 40, 360, 560, 104);
        snprintf(counter, sizeof(counter), "%03d/%03d", selected + 1, list->count);
        draw_text_right(594, 68, counter, muted, 12);

        duration = duration_seconds > 0 ? duration_seconds :
            (playing >= 0 && playing < list->count ? list->tracks[playing].duration_seconds : 0);

        draw_cover_halo(240, 100);
        draw_album_visual(240, 100, state == AUDIO_PLAYING);
        if (playing >= 0 && playing < list->count) {
            now_title = list->tracks[playing].display_name;
        } else {
            now_title = "No active track";
        }
        {
            int w = text_px_w(now_title, 2);
            int max_chars = 30;
            int x;
            if (w > max_chars * CHAR_W * 2) {
                w = max_chars * CHAR_W * 2;
            }
            x = (SCREEN_W - w) / 2;
            draw_text_scaled(x, 272, now_title, fg, max_chars, 2);
        }
        {
            char line[64];
            snprintf(line, sizeof(line), "%s  %s", state_label(state), repeat_label ? repeat_label : "Repeat All");
            draw_text((SCREEN_W - (int)strlen(line) * CHAR_W) / 2, 300, line, muted, 40);
        }
        if (eq_label && eq_label[0]) {
            char eq_line[24];
            snprintf(eq_line, sizeof(eq_line), "EQ: %s", eq_label);
            draw_text_right(594, 300, eq_line, muted, 16);
        }
        {
            char time_label[32];
            format_time_pair(elapsed_seconds, duration, time_label, sizeof(time_label));
            draw_text((SCREEN_W - (int)strlen(time_label) * CHAR_W) / 2, 314, time_label, fg, 16);
        }
        /* Timer bar: 80% lebar layar (444px), centered — sejajar label time */
        draw_progress_bar(98, 348, 444, 6, elapsed_seconds, duration, rgb(60, 70, 80), hi);
    } else {
        /* TRACK LIST — full width */
        char counter[32];
        visible = 14;
        if (selected >= visible) {
            first = selected - visible + 1;
        }

        snprintf(counter, sizeof(counter), "%03d/%03d", selected + 1, list->count);
        draw_text_right(594, 68, counter, muted, 12);

        fill_round_rect(42, 108, 556, 252, 14, panel_shadow);
        fill_round_rect(38, 104, 556, 252, 14, border);
        fill_round_rect(40, 106, 552, 248, 12, screen_bg);

        for (i = 0; i < visible && first + i < list->count; i++) {
            int idx = first + i;
            int y = 122 + i * 17;
            char line[96];
            char bitrate_label[8];
            Uint32 row_color = idx == selected ? hi_text : fg;

            if (idx == selected) {
                fill_round_rect(50, y - 5, 528, 18, 7, hi);
            }

            snprintf(line, sizeof(line), "%03d%c %s", idx + 1, list->tracks[idx].favorite ? '+' : ' ', list->tracks[idx].display_name);
            if (idx == playing) {
                draw_text(54, y, ">", row_color, 1);
            }
            if (idx == selected) {
                draw_marquee_text(70, y, line, hi_text, 55, 1);
            } else {
                draw_text(70, y, line, fg, 55);
            }
            format_bitrate_label(list->tracks[idx].bitrate_kbps, list->tracks[idx].vbr, list->tracks[idx].lossless_bits, list->tracks[idx].sample_rate, bitrate_label, sizeof(bitrate_label));
            if (bitrate_label[0]) {
                draw_text_right(574, y, bitrate_label, row_color, 6);
            }
        }
    }

    /* Info card: frosted panel, status glyph + text, folder line, a hairline
     * divider, then the volume wedge. */
    draw_glass_panel(38, 366, 556, 64, 14);
    draw_status_icon(56, 384, state, hi);
    if (message && message[0]) {
        draw_marquee_text(72, 385, message, hi, 45, 1);
    } else if (list->truncated) {
        draw_marquee_text(72, 385, "Track list truncated at 512 files", hi, 45, 1);
    } else {
        draw_text(72, 385, "Ready", hi, 10);
    }
    if (list->count > 0 && selected >= 0 && selected < list->count && list->tracks[selected].folder[0]) {
        char selected_folder[96];
        snprintf(selected_folder, sizeof(selected_folder), "Folder: %s", list->tracks[selected].folder);
        draw_marquee_text(56, 407, selected_folder, muted, 47, 0);
    } else {
        draw_text(56, 407, "A Play B Stop X Pause Y Fav R2 Settings", muted, 47);
    }
    {
        int j;
        for (j = 380; j < 418; j++) {
            int a = 60 - (j < 399 ? 399 - j : j - 399) * 3;
            blend_px(450, j, 255, 255, 255, a > 0 ? a : 0);
        }
    }
    draw_volume_wedge(466, 384, 110, 12, volume, fg, muted);
    draw_text(38, 440, "Menu to quit", muted, 32);
    {
        char version_label[48];
        snprintf(version_label, sizeof(version_label), "v%s (%s)", GARLICMP3_VERSION, GARLICMP3_GIT_HASH);
        draw_text(38 + 13 * CHAR_W, 440, version_label, muted, 32);
    }
    {
        char clock_label[8];
        format_clock_label(clock_label, sizeof(clock_label));
        if (clock_label[0]) {
            draw_text_right(594, 440, clock_label, muted, 5);
        }
    }
    SDL_UnlockSurface(screen);
    SDL_Flip(screen);
}

void ui_render_settings(const Settings *settings, AudioState state, const char *message)
{
    int i;
    int y = 116;
    Uint32 shell;
    Uint32 screen_bg;
    Uint32 border;
    Uint32 fg;
    Uint32 muted;
    Uint32 hi;
    Uint32 hi_text;
    Uint32 panel_shadow;

    if (!screen) {
        return;
    }

    shell = rgb(18, 22, 27);
    screen_bg = rgb(30, 36, 43);
    border = rgb(76, 88, 100);
    fg = rgb(235, 241, 246);
    muted = rgb(151, 163, 174);
    hi = rgb(255, 170, 0);
    hi_text = rgb(22, 24, 32);
    panel_shadow = rgb(7, 10, 13);

    if (background) {
        SDL_BlitSurface(background, NULL, screen, NULL);
    }
    SDL_LockSurface(screen);
    if (!background) {
        draw_fallback_background();
    }

    fill_round_rect(18, 14, SCREEN_W - 36, SCREEN_H - 28, 18, panel_shadow);
    fill_round_rect(22, 18, SCREEN_W - 44, SCREEN_H - 36, 16, shell);
    draw_equalizer_bg(state);

    draw_text_scaled(38, 52, "Settings", fg, 10, 2);
    draw_text_right(594, 49, state_label(state), muted, 12);

    fill_round_rect(42, 100, 556, 330, 14, panel_shadow);
    fill_round_rect(38, 96, 556, 330, 14, border);
    fill_round_rect(40, 98, 552, 326, 12, screen_bg);

    for (i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        char value[24];
        Uint32 row_color = i == settings->cursor ? hi_text : fg;

        if (settings_section_start(i)) {
            draw_text(56, y, settings_section_name(i), muted, 24);
            y += 20;
        }
        if (i == settings->cursor) {
            fill_round_rect(52, y - 10, 528, 32, 8, hi);
        }
        draw_text(72, y, settings_item_name(i), row_color, 24);

        switch (i) {
        case SETTINGS_ITEM_REPEAT:
            snprintf(value, sizeof(value), "%s", settings_repeat_name(settings->repeat_mode));
            break;
        case SETTINGS_ITEM_FAVORITES_ONLY:
            snprintf(value, sizeof(value), "%s", settings->favorites_only ? "On" : "Off");
            break;
        case SETTINGS_ITEM_VOLUME_STEP:
            snprintf(value, sizeof(value), "%d", settings->volume_step);
            break;
        case SETTINGS_ITEM_PRESET:
            snprintf(value, sizeof(value), "%s", settings_preset_name(settings->preset));
            break;
        case SETTINGS_ITEM_BASS:
            snprintf(value, sizeof(value), "%d.%d", settings->bass_t / 10, settings->bass_t % 10);
            break;
        case SETTINGS_ITEM_MID:
            snprintf(value, sizeof(value), "%d.%d", settings->mid_t / 10, settings->mid_t % 10);
            break;
        case SETTINGS_ITEM_TREBLE:
            snprintf(value, sizeof(value), "%d.%d", settings->treble_t / 10, settings->treble_t % 10);
            break;
        case SETTINGS_ITEM_RVA:
            snprintf(value, sizeof(value), "%s", settings->rva ? "On" : "Off");
            break;
        case SETTINGS_ITEM_DEBUG:
            snprintf(value, sizeof(value), "%s", settings->debug ? "On" : "Off");
            break;
        default:
            value[0] = '\0';
            break;
        }
        draw_text_right(560, y, value, row_color, 16);
        y += 28;
    }

    fill_round_rect(42, 438, 556, 22, 8, panel_shadow);
    if (message && message[0]) {
        draw_marquee_text(52, 442, message, hi, 46, 1);
    } else {
        draw_text(52, 442, "Up-Down select  Left-Right change  B close", muted, 47);
    }
    SDL_UnlockSurface(screen);
    SDL_Flip(screen);
}

static const char *const help_rows[][2] = {
    { "D-Pad Up/Down", "Navigate / folder jump (Sel)" },
    { "D-Pad L/R",     "Prev / next track" },
    { "A",             "Play" },
    { "B",             "Stop (list) / back (player)" },
    { "X",             "Pause / resume" },
    { "Y",             "Favorite toggle" },
    { "Sel+Y",         "Favorites-only mode" },
    { "Sel+Start",     "Help screen" },
    { "R2 / Sel+A",    "Settings (EQ)" },
    { "Start",         "Shuffle play" },
    { "L / R",         "Volume down / up" },
    { "Menu",          "Quit" },
};

void ui_render_help(AudioState state)
{
    int i;
    Uint32 screen_bg;
    Uint32 shell;
    Uint32 fg;
    Uint32 muted;
    Uint32 hi;
    Uint32 panel_shadow;

    if (!screen) {
        return;
    }

    screen_bg = rgb(30, 36, 43);
    shell = rgb(18, 22, 27);
    fg = rgb(235, 241, 246);
    muted = rgb(151, 163, 174);
    hi = rgb(255, 170, 0);
    panel_shadow = rgb(7, 10, 13);

    if (background) {
        SDL_BlitSurface(background, NULL, screen, NULL);
    }
    SDL_LockSurface(screen);
    if (!background) {
        draw_fallback_background();
    }

    /* Opaque full-screen wash so nothing bleeds through */
    fill_rect(0, 0, SCREEN_W, SCREEN_H, screen_bg);

    fill_round_rect(18, 14, SCREEN_W - 36, SCREEN_H - 28, 18, panel_shadow);
    fill_round_rect(22, 18, SCREEN_W - 44, SCREEN_H - 36, 16, shell);

    draw_text_scaled(38, 48, "Help", fg, 10, 2);
    draw_text_right(594, 45, state_label(state), muted, 12);

    for (i = 0; i < (int)(sizeof(help_rows) / sizeof(help_rows[0])); i++) {
        int y = 104 + i * 26;
        draw_text(54, y, help_rows[i][0], hi, 20);
        draw_text(200, y, help_rows[i][1], fg, 50);
    }

    draw_text(54, 418, "B or Select+Start to close", muted, 47);
    SDL_UnlockSurface(screen);
    SDL_Flip(screen);
}
