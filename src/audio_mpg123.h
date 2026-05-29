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
AudioState audio_state(void);
void audio_poll(void);
int audio_elapsed_seconds(void);

#endif
