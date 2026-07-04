#include <assert.h>
#include <stdio.h>

#include "audio_mpg123.h"

static void test_frame_line_updates_position(void)
{
    audio_test_reset();
    audio_test_begin_track();
    audio_test_handle_line("@F 100 200 26.12 52.24");
    assert(audio_test_get_elapsed() == 26);
    assert(audio_test_get_duration() == 78); /* 26.12 + 52.24 = 78.36 -> 78 */
}

static void test_natural_finish(void)
{
    audio_test_reset();
    audio_test_begin_track();
    audio_test_handle_line("@F 100 200 26.12 52.24");
    audio_test_handle_line("@P 0");
    assert(audio_test_get_finished() == 1);
    assert(audio_test_get_state() == AUDIO_STOPPED);
}

static void test_load_swallows_replaced_track_stop(void)
{
    audio_test_reset();
    audio_test_begin_track(); /* sets suppress_stop like audio_play does */
    audio_test_handle_line("@P 0"); /* stop event from the replaced track */
    assert(audio_test_get_finished() == 0);
    assert(audio_test_get_state() == AUDIO_PLAYING);
    audio_test_handle_line("@F 1 500 0.02 90.00");
    audio_test_handle_line("@P 0"); /* real finish */
    assert(audio_test_get_finished() == 1);
}

static void test_pause_resume_events(void)
{
    audio_test_reset();
    audio_test_begin_track();
    audio_test_handle_line("@F 1 500 0.02 90.00");
    audio_test_handle_line("@P 1");
    assert(audio_test_get_state() == AUDIO_PAUSED);
    audio_test_handle_line("@P 2");
    assert(audio_test_get_state() == AUDIO_PLAYING);
}

static void test_error_marks_track_finished_once(void)
{
    audio_test_reset();
    audio_test_begin_track();
    audio_test_handle_line("@E Cannot open file");
    assert(audio_test_get_finished() == 1);
    assert(audio_test_get_state() == AUDIO_STOPPED);
    audio_test_clear_finished(); /* main consumed it via audio_take_finished */
    audio_test_handle_line("@P 0"); /* trailing stop after the error */
    assert(audio_test_get_finished() == 0); /* must not double-advance */
}

static void test_headroom(void)
{
    assert(audio_headroom_volume_pct(10, 10, 10) == 100);
    assert(audio_headroom_volume_pct(16, 10, 10) == 62);
    assert(audio_headroom_volume_pct(20, 20, 20) == 50);
    assert(audio_headroom_volume_pct(5, 8, 10) == 100);
}

int main(void)
{
    test_frame_line_updates_position();
    test_natural_finish();
    test_load_swallows_replaced_track_stop();
    test_pause_resume_events();
    test_error_marks_track_finished_once();
    test_headroom();
    printf("test_audio OK\n");
    return 0;
}
