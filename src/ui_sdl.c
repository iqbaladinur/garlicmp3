#include "ui_sdl.h"

#include <stdio.h>
#include <string.h>

#define SCREEN_W 640
#define SCREEN_H 480
#define CHAR_W 8
#define CHAR_H 8

static SDL_Surface *screen = NULL;

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
    case '/':
        return 1 << (4 - (row > 4 ? 4 : row));
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

static void draw_text(int x, int y, const char *text, Uint32 fg, int max_chars)
{
    int i;

    if (!text) {
        return;
    }

    for (i = 0; text[i] && (max_chars <= 0 || i < max_chars); i++) {
        draw_char(x + i * CHAR_W, y, text[i], fg);
    }
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
    SDL_WM_SetCaption("Garlic MP3", "Garlic MP3");
    return 0;
}

void ui_shutdown(void)
{
    screen = NULL;
}

void ui_render(const TrackList *list, int selected, AudioState state, const char *message)
{
    int i;
    int first = 0;
    int visible = 43;
    Uint32 bg;
    Uint32 panel;
    Uint32 fg;
    Uint32 muted;
    Uint32 hi;

    if (!screen) {
        return;
    }

    bg = rgb(14, 18, 22);
    panel = rgb(31, 38, 45);
    fg = rgb(230, 235, 238);
    muted = rgb(148, 158, 166);
    hi = rgb(41, 123, 176);

    SDL_LockSurface(screen);
    fill_rect(0, 0, SCREEN_W, SCREEN_H, bg);
    fill_rect(0, 0, SCREEN_W, 32, panel);
    fill_rect(0, SCREEN_H - 34, SCREEN_W, 34, panel);

    draw_text(10, 12, "Garlic MP3", fg, 32);
    draw_text(520, 12, state_label(state), fg, 14);

    if (list->count == 0) {
        draw_text(10, 60, "No MP3 files found", fg, 40);
        draw_text(10, 78, "Use /mnt/mmc/MUSIC or app MUSIC folder", muted, 58);
    } else {
        char counter[32];
        if (selected >= visible) {
            first = selected - visible + 1;
        }

        snprintf(counter, sizeof(counter), "%03d/%03d", selected + 1, list->count);
        draw_text(430, 12, counter, fg, 12);

        for (i = 0; i < visible && first + i < list->count; i++) {
            int idx = first + i;
            int y = 42 + i * 9;
            char line[96];

            if (idx == selected) {
                fill_rect(6, y - 1, SCREEN_W - 12, 10, hi);
            }

            snprintf(line, sizeof(line), "%03d %s", idx + 1, list->tracks[idx].name);
            draw_text(10, y, line, idx == selected ? fg : muted, 76);
        }
    }

    if (message && message[0]) {
        draw_text(10, 430, message, fg, 76);
    } else if (list->truncated) {
        draw_text(10, 430, "Track list truncated at 512 files", fg, 76);
    }

    draw_text(10, 460, "UP/DOWN select  A play  B stop  START pause  L/R prev/next  X/Y volume  SELECT+START quit", fg, 78);
    SDL_UnlockSurface(screen);
    SDL_Flip(screen);
}
