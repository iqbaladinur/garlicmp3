#ifndef AUDIO_MPG123_H
#define AUDIO_MPG123_H

typedef enum AudioState {
    AUDIO_STOPPED = 0,
    AUDIO_PLAYING,
    AUDIO_PAUSED
} AudioState;

int audio_play(const char *path);
void audio_stop(void);
void audio_pause_toggle(void);
void audio_volume_down(void);
void audio_volume_up(void);
int audio_get_volume(void);
void audio_set_volume(int value);
AudioState audio_state(void);
void audio_poll(void);
int audio_elapsed_seconds(void);
int audio_take_finished(void);

#endif
