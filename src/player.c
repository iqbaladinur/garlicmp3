#include "player.h"

#include "audio_flac.h"

typedef enum Backend {
    BACKEND_MPG123 = 0,
    BACKEND_FLAC
} Backend;

static Backend current = BACKEND_MPG123;

static int start(const char *path, int seconds, int from_seconds)
{
    if (flac_is_path(path)) {
        /* mpg123 keeps the ALSA device open after STOP, and the RG35XX has
         * no software mixing, so out123 could not open it ("cannot open
         * device default"). Quit mpg123 entirely (and wait for it) to free
         * the device; the next MP3 play respawns it with the same settings. */
        audio_shutdown();
        current = BACKEND_FLAC;
        return flac_play(path, seconds);
    }
    if (current != BACKEND_MPG123) {
        /* Cut FLAC output now so mpg123 can open the audio device. */
        flac_stop(0);
    }
    current = BACKEND_MPG123;
    return from_seconds ? audio_play_from_seconds(path, seconds) : audio_play(path);
}

int player_play(const char *path)
{
    return start(path, 0, 0);
}

int player_play_from_seconds(const char *path, int seconds)
{
    return start(path, seconds, 1);
}

void player_stop(void)
{
    if (current == BACKEND_FLAC) {
        flac_stop(0);
    } else {
        audio_stop();
    }
}

void player_pause_toggle(void)
{
    if (current == BACKEND_FLAC) {
        flac_pause_toggle();
    } else {
        audio_pause_toggle();
    }
}

AudioState player_state(void)
{
    return current == BACKEND_FLAC ? flac_state() : audio_state();
}

void player_poll(void)
{
    audio_poll();
    flac_poll();
}

int player_elapsed_seconds(void)
{
    return current == BACKEND_FLAC ? flac_elapsed_ms() / 1000 : audio_elapsed_seconds();
}

int player_elapsed_ms(void)
{
    return current == BACKEND_FLAC ? flac_elapsed_ms() : audio_elapsed_ms();
}

int player_duration_seconds(void)
{
    return current == BACKEND_FLAC ? flac_duration_seconds() : audio_duration_seconds();
}

int player_take_finished(void)
{
    /* Drain both so a stale finish from the idle path never leaks later. */
    int mp3 = audio_take_finished();
    int flac = flac_take_finished();
    return current == BACKEND_FLAC ? flac : mp3;
}

const char *player_last_error(void)
{
    return current == BACKEND_FLAC ? flac_last_error() : audio_last_error();
}

void player_set_eq(int bass_tenths, int mid_tenths, int treble_tenths)
{
    audio_set_eq(bass_tenths, mid_tenths, treble_tenths);
    flac_set_eq(bass_tenths, mid_tenths, treble_tenths);
}

void player_set_rva(int on)
{
    audio_set_rva(on);
    flac_set_rva(on);
}

void player_shutdown(void)
{
    flac_shutdown();
}
