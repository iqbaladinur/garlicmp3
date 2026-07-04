#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "audio_mpg123.h"

static int wait_finished(int max_ticks)
{
    int i;

    for (i = 0; i < max_ticks; i++) {
        if (audio_take_finished()) {
            return 1;
        }
        usleep(10000);
    }
    return 0;
}

int main(void)
{
    const char *test_mp3;

    if (audio_init() != 0) {
        printf("SKIP: %s (install mpg123 to run this test)\n", audio_last_error());
        return 0;
    }

    /* A missing file must surface as a finished track (auto-skip path)
     * and must not kill the mpg123 process. */
    assert(audio_play("/nonexistent/garlic-test.mp3") == 0);
    assert(wait_finished(200) == 1);
    assert(audio_state() == AUDIO_STOPPED);

    /* EQ/RVA commands must be accepted; the process must survive them. */
    audio_set_eq(16, 10, 14);
    audio_set_rva(1);
    usleep(100000);
    audio_poll();
    assert(audio_play("/nonexistent/garlic-test2.mp3") == 0); /* pipe still alive */
    assert(wait_finished(200) == 1);

    test_mp3 = getenv("TEST_MP3");
    if (test_mp3 && test_mp3[0]) {
        assert(audio_play(test_mp3) == 0);
        sleep(1);
        assert(audio_state() == AUDIO_PLAYING);
        assert(audio_duration_seconds() > 0);
        assert(audio_elapsed_seconds() >= 0);
        audio_pause_toggle();
        usleep(300000);
        assert(audio_state() == AUDIO_PAUSED);
        audio_pause_toggle();
        usleep(300000);
        assert(audio_state() == AUDIO_PLAYING);
        audio_stop();
        usleep(300000);
        assert(audio_state() == AUDIO_STOPPED);
        assert(audio_take_finished() == 0); /* STOP is not a natural finish */
    } else {
        printf("note: set TEST_MP3=/path/to.mp3 for playback checks\n");
    }

    audio_shutdown();
    printf("test_audio_integration OK\n");
    return 0;
}
