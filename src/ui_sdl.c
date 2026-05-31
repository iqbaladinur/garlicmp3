#include "ui_sdl.h"

#include <stdio.h>
#include <string.h>

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

static void draw_text_right(int right_x, int y, const char *text, Uint32 fg, int max_chars)
{
    int len;

    if (!text) {
        return;
    }

    len = (int)strlen(text);
    if (max_chars > 0 && len > max_chars) {
        len = max_chars;
    }
    draw_text(right_x - len * CHAR_W, y, text, fg, max_chars);
}

static void draw_marquee_text(int x, int y, const char *text, Uint32 fg, int max_chars, int active)
{
    int len;
    int offset = 0;
    char window[96];

    if (!text || max_chars <= 0) {
        return;
    }

    len = (int)strlen(text);
    if (!active || len <= max_chars) {
        draw_text(x, y, text, fg, max_chars);
        return;
    }

    offset = (int)((SDL_GetTicks() / 220) % (Uint32)(len + 4));
    if (offset >= len) {
        offset = 0;
    }

    snprintf(window, sizeof(window), "%s    %s", text + offset, text);
    draw_text(x, y, window, fg, max_chars);
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

static void draw_text_scaled(int x, int y, const char *text, Uint32 fg, int max_chars, int scale)
{
    int i;

    if (!text) {
        return;
    }

    for (i = 0; text[i] && (max_chars <= 0 || i < max_chars); i++) {
        draw_char_scaled(x + i * CHAR_W * scale, y, text[i], fg, scale);
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

static void draw_album_visual(int x, int y, int size, Uint32 muted, int spinning)
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
    int cx = x + size / 2;
    int cy = y + size / 2;
    Uint32 art_bg = rgb(12, 16, 21);
    Uint32 accent = rgb(33, 145, 226);
    Uint32 disc = rgb(229, 236, 241);
    Uint32 ring = rgb(202, 212, 220);
    Uint32 shine = rgb(246, 249, 251);

    (void)muted;
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

    fill_round_rect(x, y, size, size, 12, rgb(5, 8, 11));
    fill_round_rect(x + 2, y + 2, size - 4, size - 4, 10, art_bg);
    fill_circle(cx + 3, cy + 3, 42, rgb(3, 6, 9));
    fill_circle(cx, cy, 42, disc);
    fill_circle(cx, cy, 31, ring);
    fill_circle(cx, cy, 21, disc);
    fill_circle(cx, cy, 12, art_bg);
    fill_circle(cx, cy, 5, accent);
    fill_circle(cx + marker_x[frame] / 2, cy + marker_y[frame] / 2, 6, shine);
    fill_circle(cx + marker_x[frame], cy + marker_y[frame], 4, art_bg);
    fill_circle(cx + marker_x[frame], cy + marker_y[frame], 2, accent);
}

static void draw_volume_bar(int x, int y, int w, int h, int volume, Uint32 fg, Uint32 muted, Uint32 hi)
{
    char label[16];
    int fill_w;

    if (volume < 0) {
        volume = 0;
    }
    if (volume > UI_VOLUME_MAX) {
        volume = UI_VOLUME_MAX;
    }

    snprintf(label, sizeof(label), "VOL %02d", volume);
    draw_text(x, y, label, fg, 8);
    fill_round_rect(x, y + 18, w, h, 4, muted);
    fill_w = (w * volume) / UI_VOLUME_MAX;
    if (fill_w > 0) {
        fill_round_rect(x, y + 18, fill_w, h, 4, hi);
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

void ui_render(const TrackList *list, int selected, int playing, AudioState state, int elapsed_seconds, int volume, const char *repeat_label, int favorites_only, const char *message)
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
    Uint32 info_bg;
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
    hi = rgb(33, 145, 226);
    hi_text = rgb(252, 254, 255);
    info_bg = rgb(24, 30, 36);
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

    draw_text_scaled(38, 52, "Garlic MP3", fg, 10, 2);
    draw_text_right(594, 49, state_label(state), muted, 12);
    draw_text(38, 74, repeat_label ? repeat_label : "Repeat All", muted, 14);
    if (favorites_only) {
        draw_text(158, 74, "Favorites", hi, 12);
    }

    if (list->count == 0) {
        fill_round_rect(42, 108, 556, 252, 14, panel_shadow);
        fill_round_rect(38, 104, 556, 252, 14, border);
        fill_round_rect(40, 106, 552, 248, 12, screen_bg);
        draw_text_scaled(72, 152, "No MP3 files", fg, 12, 2);
        draw_text(74, 192, "Use Roms/MUSIC or app MUSIC folder", muted, 58);
    } else {
        char counter[32];
        visible = 13;
        if (selected >= visible) {
            first = selected - visible + 1;
        }

        snprintf(counter, sizeof(counter), "%03d/%03d", selected + 1, list->count);
        draw_text_right(594, 68, counter, muted, 12);

        fill_round_rect(42, 108, 382, 252, 14, panel_shadow);
        fill_round_rect(38, 104, 382, 252, 14, border);
        fill_round_rect(40, 106, 378, 248, 12, screen_bg);
        fill_round_rect(439, 108, 159, 252, 14, panel_shadow);
        fill_round_rect(435, 104, 159, 252, 14, border);
        fill_round_rect(437, 106, 155, 248, 12, rgb(26, 32, 39));
        draw_album_visual(454, 122, 120, muted, state == AUDIO_PLAYING);
        draw_text(454, 260, "Now Playing", hi, 14);
        draw_text(454, 280, state_label(state), fg, 12);
        if (playing >= 0 && playing < list->count) {
            now_title = list->tracks[playing].display_name;
        } else {
            now_title = "No active track";
        }
        draw_marquee_text(454, 300, now_title, muted, 16, state == AUDIO_PLAYING || state == AUDIO_PAUSED);
        {
            char time_label[32];
            int duration = playing >= 0 && playing < list->count ? list->tracks[playing].duration_seconds : 0;
            format_time_pair(elapsed_seconds, duration, time_label, sizeof(time_label));
            draw_text(454, 324, time_label, fg, 16);
            draw_progress_bar(454, 340, 120, 4, elapsed_seconds, duration, rgb(60, 70, 80), hi_text);
        }

        for (i = 0; i < visible && first + i < list->count; i++) {
            int idx = first + i;
            int y = 122 + i * 17;
            char line[96];
            Uint32 row_color = idx == selected ? hi_text : fg;

            if (idx == selected) {
                fill_round_rect(50, y - 5, 356, 18, 7, hi);
            }

            snprintf(line, sizeof(line), "%03d%c %s", idx + 1, list->tracks[idx].favorite ? '+' : ' ', list->tracks[idx].display_name);
            if (idx == playing) {
                draw_text(54, y, ">", row_color, 1);
            }
            if (idx == selected) {
                draw_marquee_text(70, y, line, hi_text, 40, 1);
            } else {
                draw_text(70, y, line, fg, 40);
            }
        }
    }

    fill_round_rect(42, 370, 556, 66, 13, panel_shadow);
    fill_round_rect(38, 366, 556, 66, 13, border);
    fill_round_rect(40, 368, 552, 62, 11, info_bg);
    if (message && message[0]) {
        draw_marquee_text(54, 385, message, hi, 47, 1);
    } else if (list->truncated) {
        draw_marquee_text(54, 385, "Track list truncated at 512 files", hi, 47, 1);
    } else {
        draw_text(54, 385, "Ready", hi, 10);
    }
    if (list->count > 0 && selected >= 0 && selected < list->count && list->tracks[selected].folder[0]) {
        char selected_folder[96];
        snprintf(selected_folder, sizeof(selected_folder), "Folder: %s", list->tracks[selected].folder);
        draw_marquee_text(54, 405, selected_folder, muted, 47, 0);
    } else {
        draw_text(54, 405, "A Play B Stop X Pause Y Fav Sel+Y Filter", muted, 47);
    }
    draw_volume_bar(468, 385, 92, 8, volume, fg, rgb(60, 70, 80), hi);
    draw_text(38, 440, "Menu to quit", muted, 32);
    SDL_UnlockSurface(screen);
    SDL_Flip(screen);
}
