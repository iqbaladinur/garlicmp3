/* Overlap detector v2: ui_sdl.c primitives renamed via sed to track_*,
 * then implemented here as bounding-box recorders. Deterministic overlap
 * detection without vision.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <SDL/SDL.h>

#define MAX_BOXES 2048
typedef struct {
    int x, y, w, h;
    int is_text;
    char label[48];
} Box;
static Box boxes[MAX_BOXES];
static int nboxes = 0;

static void track(int x, int y, int w, int h, int is_text, const char *label)
{
    if (nboxes < MAX_BOXES) {
        boxes[nboxes].x = x;
        boxes[nboxes].y = y;
        boxes[nboxes].w = w;
        boxes[nboxes].h = h;
        boxes[nboxes].is_text = is_text;
        snprintf(boxes[nboxes].label, sizeof(boxes[nboxes].label), "%s", label ? label : "?");
        nboxes++;
    }
}

/* --- renamed primitives from ui_sdl.c (track_*) --- */
static void track_fill_rect(int x, int y, int w, int h, Uint32 c)
{ (void)c; track(x, y, w, h, 0, "rect"); }
static void track_fill_round_rect(int x, int y, int w, int h, int r, Uint32 c)
{ (void)c; (void)r; track(x, y, w, h, 0, "round"); }
static void track_fill_circle(int cx, int cy, int radius, Uint32 c)
{ (void)c; track(cx - radius, cy - radius, radius * 2, radius * 2, 0, "circle"); }
static void track_draw_char(int x, int y, char c, Uint32 fg)
{ (void)c; (void)fg; track(x, y, 8, 8, 1, "char"); }
static void track_draw_char_scaled(int x, int y, char c, Uint32 fg, int scale)
{ (void)c; (void)fg; track(x, y, 8 * scale, 8 * scale, 1, "char_s"); }
static void track_draw_text(int x, int y, const char *text, Uint32 fg, int max_chars)
{
    int len = text ? (int)strlen(text) : 0;
    (void)fg;
    if (max_chars > 0 && len > max_chars) len = max_chars;
    track(x, y, len * 8, 8, 1, "text");
}
static void track_draw_text_right(int right_x, int y, const char *text, Uint32 fg, int max_chars)
{
    int len = text ? (int)strlen(text) : 0;
    (void)fg;
    if (max_chars > 0 && len > max_chars) len = max_chars;
    track(right_x - len * 8, y, len * 8, 8, 1, "text_r");
}
static void track_draw_text_scaled(int x, int y, const char *text, Uint32 fg, int max_chars, int scale)
{
    int len = text ? (int)strlen(text) : 0;
    (void)fg;
    if (max_chars > 0 && len > max_chars) len = max_chars;
    track(x, y, len * 8 * scale, 8 * scale, 1, "text_s");
}
static void track_draw_marquee_text(int x, int y, const char *text, Uint32 fg, int max_chars, int active)
{
    int len = text ? (int)strlen(text) : 0;
    (void)fg; (void)active;
    if (max_chars > 0) len = max_chars;
    track(x, y, len * 8, 8, 1, "marquee");
}

#include "ui_sdl_track.c"

static int overlaps(const Box *a, const Box *b)
{
    return a->x < b->x + b->w && b->x < a->x + a->w &&
           a->y < b->y + b->h && b->y < a->y + a->h;
}

static void init_list(TrackList *tl)
{
    static const char *names[] = {
        "Midnight City", "Blinding Lights",
        "Rock'n Roll, Morning Light Falls on You", "Disconnected",
        "Vapor Trail", "Synthwave Dreams", "Neon Drive",
        "Sunset Boulevard", "Crimson Tide", "Golden Hour",
        "Electric Youth", "Faded Memories"
    };
    int i;
    memset(tl, 0, sizeof(*tl));
    for (i = 0; i < 12; i++) {
        snprintf(tl->tracks[i].display_name, sizeof(tl->tracks[i].display_name), "%s", names[i]);
        snprintf(tl->tracks[i].folder, sizeof(tl->tracks[i].folder), "80s Hits");
        tl->tracks[i].duration_seconds = 180 + i * 37;
        tl->tracks[i].bitrate_kbps = (i % 3 == 0) ? 128 : ((i % 3 == 1) ? 320 : 192);
        tl->tracks[i].vbr = (i % 4 == 0);
        tl->tracks[i].favorite = (i == 2 || i == 7);
    }
    tl->count = 12;
}

static void report(const char *name)
{
    int i, j, found = 0;
    printf("===== %s (%d boxes) =====\n", name, nboxes);
    for (i = 0; i < nboxes; i++) {
        if (boxes[i].is_text &&
            (boxes[i].x < 22 || boxes[i].x + boxes[i].w > 618 ||
             boxes[i].y < 18 || boxes[i].y + boxes[i].h > 462)) {
            printf("OUTSIDE-SHELL: %s (%d,%d %dx%d)\n", boxes[i].label,
                   boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h);
            found++;
        }
        for (j = i + 1; j < nboxes; j++) {
            if (boxes[i].is_text && boxes[j].is_text && overlaps(&boxes[i], &boxes[j])) {
                printf("TEXT-OVERLAP: %s(%d,%d %dx%d) <-> %s(%d,%d %dx%d)\n",
                       boxes[i].label, boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h,
                       boxes[j].label, boxes[j].x, boxes[j].y, boxes[j].w, boxes[j].h);
                found++;
            }
        }
    }
    if (!found) {
        printf("CLEAN\n");
    }
    nboxes = 0;
}

int main(void)
{
    TrackList tl;
    Settings s;

    if (ui_init() != 0) {
        fprintf(stderr, "ui_init failed\n");
        return 1;
    }
    init_list(&tl);

    ui_render(&tl, 2, 3, AUDIO_PLAYING, 83, 296, 28, "Repeat All", "Rock", 0, "Ready");
    report("library");

    settings_init(&s);
    s.cursor = SETTINGS_ITEM_REPEAT;
    ui_render_settings(&s, AUDIO_PLAYING, "");
    report("settings");

    ui_render_help(AUDIO_STOPPED);
    report("help");

    return 0;
}
