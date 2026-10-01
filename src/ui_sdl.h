#ifndef UI_SDL_H
#define UI_SDL_H

#include "audio_mpg123.h"
#include "file_scan.h"
#include "settings.h"

#include <SDL/SDL.h>

int ui_init(void);
void ui_shutdown(void);
void ui_render(const TrackList *list, int selected, int playing, AudioState state, int elapsed_seconds, int duration_seconds, int volume, const char *repeat_label, const char *eq_label, int favorites_only, const char *message, int view_mode);
void ui_render_settings(const Settings *settings, AudioState state, const char *message);
void ui_render_help(AudioState state);

/* Real spectrum for the now-playing visualizer. The source returns 1 and
 * fills up to nbands levels (0..255, low to high frequency) for playback
 * time ms, or 0 when it has no data yet (the UI then animates on its own). */
typedef int (*UiSpectrumSource)(int ms, unsigned char *bands, int nbands);
void ui_set_spectrum_source(UiSpectrumSource source, int latency_ms);
/* Playback position in ms (from mpg123 @F); smoothed between updates. */
void ui_set_playback_ms(int ms);

#endif
