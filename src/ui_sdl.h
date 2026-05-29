#ifndef UI_SDL_H
#define UI_SDL_H

#include "audio_mpg123.h"
#include "file_scan.h"

#include <SDL/SDL.h>

int ui_init(void);
void ui_shutdown(void);
void ui_render(const TrackList *list, int selected, int playing, AudioState state, int elapsed_seconds, int volume, const char *message);

#endif
