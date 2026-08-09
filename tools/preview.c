/* Preview harness: renders garlicmp3 UI screens to BMP using the REAL ui_sdl.c.
 * Built inside the toolchain container, run with SDL_VIDEODRIVER=dummy.
 */
#include <stdio.h>
#include <string.h>

#include "ui_sdl.h"
#include "version.h"

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

int main(void)
{
    TrackList tl;
    Settings s;

    printf("GarlicMP3 preview %s (%s)\n", GARLICMP3_VERSION, GARLICMP3_GIT_HASH);
    if (ui_init() != 0) {
        fprintf(stderr, "ui_init failed\n");
        return 1;
    }

    init_list(&tl);

    /* 1. Library screen, playing state, selected track 3 */
    ui_render(&tl, 2, 3, AUDIO_PLAYING, 83, 296, 28, "Repeat All", "Rock", 0, "Ready");
    SDL_SaveBMP(SDL_GetVideoSurface(), "/out/1_library.bmp");
    printf("saved 1_library.bmp\n");

    /* 2. Settings screen, cursor on Repeat */
    settings_init(&s);
    s.cursor = SETTINGS_ITEM_REPEAT;
    ui_render_settings(&s, AUDIO_PLAYING, "");
    SDL_SaveBMP(SDL_GetVideoSurface(), "/out/2_settings.bmp");
    printf("saved 2_settings.bmp\n");

    /* 3. Help overlay */
    ui_render_help(AUDIO_STOPPED);
    SDL_SaveBMP(SDL_GetVideoSurface(), "/out/3_help.bmp");
    printf("saved 3_help.bmp\n");

    return 0;
}
