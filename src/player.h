#ifndef PLAYER_H
#define PLAYER_H

#include "audio_mpg123.h"

/* Routes playback to one of two independent paths by file type:
 *   .mp3  -> mpg123 -R child (audio_mpg123.c, unchanged)
 *   .flac -> dr_flac decoder thread + out123 child (audio_flac.c)
 * Only one path plays at a time; switching stops the other one first, and an
 * error on one path never touches the other. Volume, EQ and RVA settings
 * remain audio_* calls (system volume / mpg123 only). */

int player_play(const char *path);
int player_play_from_seconds(const char *path, int seconds);
void player_stop(void);
void player_pause_toggle(void);
AudioState player_state(void);
void player_poll(void);
int player_elapsed_seconds(void);
int player_elapsed_ms(void);
int player_duration_seconds(void);
int player_take_finished(void);
const char *player_last_error(void);
void player_shutdown(void);

#endif
