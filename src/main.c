#include "audio_mpg123.h"
#include "file_scan.h"
#include "input.h"
#include "ui_sdl.h"

#include <SDL/SDL.h>
#include <stdio.h>
#include <string.h>

static int play_selected(const TrackList *list, int selected, char *message, size_t message_size)
{
    if (list->count <= 0 || selected < 0 || selected >= list->count) {
        snprintf(message, message_size, "No track selected");
        return -1;
    }

    if (audio_play(list->tracks[selected].path) == 0) {
        snprintf(message, message_size, "Playing: %s", list->tracks[selected].name);
        return selected;
    } else {
        snprintf(message, message_size, "Failed to start mpg123");
        return -1;
    }
}

static void clamp_selected(const TrackList *list, int *selected)
{
    if (list->count <= 0) {
        *selected = 0;
    } else if (*selected < 0) {
        *selected = list->count - 1;
    } else if (*selected >= list->count) {
        *selected = 0;
    }
}

static void handle_action(InputAction action, const TrackList *list, int *selected, int *playing, int *running, char *message, size_t message_size)
{
    if (action != ACTION_NONE) {
        printf("Action=%d selected=%d tracks=%d\n", action, *selected, list->count);
    }

    switch (action) {
    case ACTION_UP:
        (*selected)--;
        clamp_selected(list, selected);
        if (list->count <= 0) {
            snprintf(message, message_size, "No MP3 files found");
        }
        break;
    case ACTION_DOWN:
        (*selected)++;
        clamp_selected(list, selected);
        if (list->count <= 0) {
            snprintf(message, message_size, "No MP3 files found");
        }
        break;
    case ACTION_PLAY:
        *playing = play_selected(list, *selected, message, message_size);
        break;
    case ACTION_STOP:
        audio_stop();
        *playing = -1;
        snprintf(message, message_size, "Stopped");
        break;
    case ACTION_PAUSE:
        audio_pause_toggle();
        snprintf(message, message_size, "Pause/resume");
        break;
    case ACTION_PREV:
        if (list->count > 0) {
            (*selected)--;
            clamp_selected(list, selected);
            *playing = play_selected(list, *selected, message, message_size);
        }
        break;
    case ACTION_NEXT:
        if (list->count > 0) {
            (*selected)++;
            clamp_selected(list, selected);
            *playing = play_selected(list, *selected, message, message_size);
        }
        break;
    case ACTION_VOL_DOWN:
        audio_volume_down();
        snprintf(message, message_size, "Volume down");
        break;
    case ACTION_VOL_UP:
        audio_volume_up();
        snprintf(message, message_size, "Volume up");
        break;
    case ACTION_QUIT:
        *running = 0;
        break;
    default:
        break;
    }
}

int main(int argc, char **argv)
{
    TrackList list;
    SDL_Event event;
    int running = 1;
    int selected = 0;
    int playing = -1;
    Uint32 last_log = 0;
    Uint32 started_at = 0;
    Uint32 auto_quit_ms = 0;
    char message[128] = "";

    (void)argc;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("main start\n");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    input_init();
    printf("input init done\n");

    if (ui_init() != 0) {
        input_shutdown();
        SDL_Quit();
        return 1;
    }
    printf("ui init done\n");

    scan_music(&list, argv && argv[0] ? argv[0] : NULL);
    printf("scan done tracks=%d truncated=%d\n", list.count, list.truncated);
    if (list.count == 0) {
        snprintf(message, sizeof(message), "No MP3 files found");
    } else {
        snprintf(message, sizeof(message), "%d tracks found", list.count);
    }

    started_at = SDL_GetTicks();

    while (running) {
        audio_poll();

        /* evdev thread input */
        {
            InputAction action = input_poll_joystick();
            handle_action(action, &list, &selected, &playing, &running, message, sizeof(message));
        }

        /* SDL event queue (keyboard fallback / SDL_QUIT) */
        while (SDL_PollEvent(&event)) {
            InputAction action = input_event_to_action(&event);
            handle_action(action, &list, &selected, &playing, &running, message, sizeof(message));
        }

        if (audio_state() == AUDIO_STOPPED) {
            playing = -1;
        }

        if (SDL_GetTicks() - last_log > 10000) {
            printf("Heartbeat selected=%d tracks=%d state=%d\n", selected, list.count, audio_state());
            last_log = SDL_GetTicks();
        }

        if (auto_quit_ms > 0 && SDL_GetTicks() - started_at > auto_quit_ms) {
            printf("Auto quit after %u ms\n", auto_quit_ms);
            running = 0;
        }

        ui_render(&list, selected, playing, audio_state(), audio_elapsed_seconds(), message);
        SDL_Delay(33);
    }

    audio_stop();
    ui_shutdown();
    input_shutdown();
    SDL_Quit();
    return 0;
}
