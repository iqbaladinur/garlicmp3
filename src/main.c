#include "audio_mpg123.h"
#include "file_scan.h"
#include "input.h"
#include "ui_sdl.h"

#include <SDL/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RESUME_SKIP_END_SECONDS 10
#define SHUFFLE_RECENT_MAX 8

typedef struct AppState {
    char path[TRACK_PATH_MAX];
    char playing_path[TRACK_PATH_MAX];
    int volume;
    int resume_play;
    int elapsed_seconds;
    int repeat_mode;
    int debug;
} AppState;

typedef struct ShuffleHistory {
    int items[SHUFFLE_RECENT_MAX];
    int count;
    int pos;
} ShuffleHistory;

typedef enum RepeatMode {
    REPEAT_OFF = 0,
    REPEAT_ALL = 1,
    REPEAT_ONE = 2
} RepeatMode;

static void state_file_path(char *out, size_t out_size, const char *argv0)
{
    const char *slash;

    if (!argv0 || !argv0[0]) {
        snprintf(out, out_size, "state.cfg");
        return;
    }

    slash = strrchr(argv0, '/');
    if (!slash) {
        snprintf(out, out_size, "state.cfg");
        return;
    }

    snprintf(out, out_size, "%.*s/state.cfg", (int)(slash - argv0), argv0);
}

static void trim_line(char *s)
{
    size_t n;

    if (!s) {
        return;
    }

    n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static void load_state(const char *path, AppState *state)
{
    FILE *fp;
    char line[TRACK_PATH_MAX + 64];

    memset(state, 0, sizeof(*state));
    state->volume = -1;
    state->repeat_mode = REPEAT_ALL;

    fp = fopen(path, "r");
    if (!fp) {
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        trim_line(line);
        if (strncmp(line, "selected_path=", 14) == 0) {
            snprintf(state->path, sizeof(state->path), "%s", line + 14);
        } else if (strncmp(line, "playing_path=", 13) == 0) {
            snprintf(state->playing_path, sizeof(state->playing_path), "%s", line + 13);
        } else if (strncmp(line, "resume_play=", 12) == 0) {
            state->resume_play = atoi(line + 12);
        } else if (strncmp(line, "elapsed=", 8) == 0) {
            state->elapsed_seconds = atoi(line + 8);
        } else if (strncmp(line, "repeat_mode=", 12) == 0) {
            state->repeat_mode = atoi(line + 12);
            if (state->repeat_mode < REPEAT_OFF || state->repeat_mode > REPEAT_ONE) {
                state->repeat_mode = REPEAT_ALL;
            }
        } else if (strncmp(line, "debug=", 6) == 0) {
            state->debug = atoi(line + 6) ? 1 : 0;
        } else if (strncmp(line, "volume=", 7) == 0) {
            state->volume = atoi(line + 7);
        }
    }

    fclose(fp);
}

static void save_state(const char *path, const TrackList *list, int selected, int playing, int repeat_mode, int debug)
{
    FILE *fp;
    AudioState state_now;

    fp = fopen(path, "w");
    if (!fp) {
        perror("state save");
        return;
    }

    fprintf(fp, "version=1\n");
    if (list->count > 0 && selected >= 0 && selected < list->count) {
        fprintf(fp, "selected_path=%s\n", list->tracks[selected].path);
    }
    state_now = audio_state();
    if (list->count > 0 && playing >= 0 && playing < list->count && state_now != AUDIO_STOPPED) {
        fprintf(fp, "playing_path=%s\n", list->tracks[playing].path);
        fprintf(fp, "resume_play=1\n");
        fprintf(fp, "elapsed=%d\n", audio_elapsed_seconds());
    } else {
        fprintf(fp, "resume_play=0\n");
        fprintf(fp, "elapsed=0\n");
    }
    fprintf(fp, "repeat_mode=%d\n", repeat_mode);
    fprintf(fp, "debug=%d\n", debug ? 1 : 0);
    fprintf(fp, "volume=%d\n", audio_get_volume());
    fclose(fp);
}

static const char *repeat_label(int repeat_mode)
{
    switch (repeat_mode) {
    case REPEAT_OFF:
        return "Repeat Off";
    case REPEAT_ONE:
        return "Repeat One";
    default:
        return "Repeat All";
    }
}

static int find_track_by_path(const TrackList *list, const char *path)
{
    int i;

    if (!path || !path[0]) {
        return -1;
    }

    for (i = 0; i < list->count; i++) {
        if (strcmp(list->tracks[i].path, path) == 0) {
            return i;
        }
    }

    return -1;
}

static int play_selected(const TrackList *list, int selected, char *message, size_t message_size)
{
    if (list->count <= 0 || selected < 0 || selected >= list->count) {
        snprintf(message, message_size, "No track selected");
        return -1;
    }

    if (audio_play(list->tracks[selected].path) == 0) {
        snprintf(message, message_size, "Playing: %s", list->tracks[selected].display_name);
        return selected;
    } else {
        snprintf(message, message_size, "Failed to start mpg123");
        return -1;
    }
}

static int resume_selected(const TrackList *list, int selected, int elapsed_seconds, char *message, size_t message_size)
{
    if (list->count <= 0 || selected < 0 || selected >= list->count) {
        snprintf(message, message_size, "No track selected");
        return -1;
    }
    if (list->tracks[selected].duration_seconds > 0 &&
        elapsed_seconds >= list->tracks[selected].duration_seconds - RESUME_SKIP_END_SECONDS) {
        elapsed_seconds = 0;
        snprintf(message, message_size, "Restarted: %s", list->tracks[selected].display_name);
    }

    if (audio_play_from_seconds(list->tracks[selected].path, elapsed_seconds) == 0) {
        if (elapsed_seconds > 0) {
            snprintf(message, message_size, "Resumed: %s", list->tracks[selected].display_name);
        }
        return selected;
    }

    snprintf(message, message_size, "Failed to resume mpg123");
    return -1;
}

static int shuffle_history_contains(const ShuffleHistory *history, int idx)
{
    int i;

    for (i = 0; i < history->count; i++) {
        if (history->items[i] == idx) {
            return 1;
        }
    }
    return 0;
}

static void shuffle_history_add(ShuffleHistory *history, int idx)
{
    if (idx < 0) {
        return;
    }

    if (history->count < SHUFFLE_RECENT_MAX) {
        history->items[history->count++] = idx;
    } else {
        history->items[history->pos] = idx;
        history->pos = (history->pos + 1) % SHUFFLE_RECENT_MAX;
    }
}

static int random_track_index(const TrackList *list, int current, const ShuffleHistory *history)
{
    int i;
    int next;
    int candidates[TRACK_MAX];
    int candidate_count = 0;

    if (list->count <= 0) {
        return -1;
    }
    if (list->count == 1) {
        return 0;
    }

    for (i = 0; i < list->count; i++) {
        if (i != current && !shuffle_history_contains(history, i)) {
            candidates[candidate_count++] = i;
        }
    }

    if (candidate_count <= 0) {
        for (i = 0; i < list->count; i++) {
            if (i != current) {
                candidates[candidate_count++] = i;
            }
        }
    }

    if (candidate_count <= 0) {
        return current;
    }

    next = rand() % candidate_count;
    return candidates[next];
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

static int handle_action(InputAction action, const TrackList *list, int *selected, int *playing, int *repeat_mode, int *running, int debug, ShuffleHistory *shuffle_history, char *message, size_t message_size)
{
    int save_needed = 0;

    if (debug && action != ACTION_NONE) {
        printf("Action=%d selected=%d tracks=%d\n", action, *selected, list->count);
    }

    switch (action) {
    case ACTION_UP:
        (*selected)--;
        clamp_selected(list, selected);
        if (list->count <= 0) {
            snprintf(message, message_size, "No MP3 files found");
        }
        save_needed = 1;
        break;
    case ACTION_DOWN:
        (*selected)++;
        clamp_selected(list, selected);
        if (list->count <= 0) {
            snprintf(message, message_size, "No MP3 files found");
        }
        save_needed = 1;
        break;
    case ACTION_PLAY:
        *playing = play_selected(list, *selected, message, message_size);
        shuffle_history_add(shuffle_history, *playing);
        save_needed = *playing >= 0;
        break;
    case ACTION_STOP:
        audio_stop();
        *playing = -1;
        snprintf(message, message_size, "Stopped");
        save_needed = 1;
        break;
    case ACTION_PAUSE:
        audio_pause_toggle();
        snprintf(message, message_size, "Pause/resume");
        break;
    case ACTION_SHUFFLE_PLAY:
        if (list->count > 0) {
            int next = random_track_index(list, *playing >= 0 ? *playing : *selected, shuffle_history);
            if (next >= 0) {
                *selected = next;
                *playing = play_selected(list, *selected, message, message_size);
                shuffle_history_add(shuffle_history, *playing);
                save_needed = *playing >= 0;
            }
        } else {
            snprintf(message, message_size, "No MP3 files found");
        }
        break;
    case ACTION_REPEAT_TOGGLE:
        *repeat_mode = (*repeat_mode + 1) % 3;
        snprintf(message, message_size, "%s", repeat_label(*repeat_mode));
        save_needed = 1;
        break;
    case ACTION_PREV:
        if (list->count > 0) {
            (*selected)--;
            clamp_selected(list, selected);
            *playing = play_selected(list, *selected, message, message_size);
            shuffle_history_add(shuffle_history, *playing);
            save_needed = 1;
        }
        break;
    case ACTION_NEXT:
        if (list->count > 0) {
            (*selected)++;
            clamp_selected(list, selected);
            *playing = play_selected(list, *selected, message, message_size);
            shuffle_history_add(shuffle_history, *playing);
            save_needed = 1;
        }
        break;
    case ACTION_VOL_DOWN:
        audio_volume_down();
        snprintf(message, message_size, "Volume down");
        save_needed = 1;
        break;
    case ACTION_VOL_UP:
        audio_volume_up();
        snprintf(message, message_size, "Volume up");
        save_needed = 1;
        break;
    case ACTION_QUIT:
        if (audio_state() == AUDIO_PLAYING) {
            audio_pause_toggle();
            snprintf(message, message_size, "Paused for resume");
        }
        *running = 0;
        save_needed = 1;
        break;
    default:
        break;
    }

    return save_needed;
}

int main(int argc, char **argv)
{
    TrackList list;
    SDL_Event event;
    int running = 1;
    int selected = 0;
    int playing = -1;
    int repeat_mode = REPEAT_ALL;
    int debug = 0;
    Uint32 last_log = 0;
    Uint32 last_state_save = 0;
    Uint32 started_at = 0;
    Uint32 auto_quit_ms = 0;
    char message[128] = "";
    char state_path[TRACK_PATH_MAX];
    AppState saved_state;
    ShuffleHistory shuffle_history;

    (void)argc;

    memset(&shuffle_history, 0, sizeof(shuffle_history));
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("main start\n");
    srand((unsigned int)time(NULL));
    state_file_path(state_path, sizeof(state_path), argv && argv[0] ? argv[0] : NULL);
    load_state(state_path, &saved_state);
    repeat_mode = saved_state.repeat_mode;
    debug = saved_state.debug;
    printf("state file: %s saved_volume=%d debug=%d\n", state_path, saved_state.volume, debug);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    input_init();
    input_set_debug(debug);
    printf("input init done\n");

    if (ui_init() != 0) {
        input_shutdown();
        SDL_Quit();
        return 1;
    }
    printf("ui init done\n");

    scan_music(&list, argv && argv[0] ? argv[0] : NULL);
    printf("scan done tracks=%d truncated=%d\n", list.count, list.truncated);
    if (saved_state.volume >= 0) {
        audio_set_volume(saved_state.volume);
    }
    if (list.count > 0) {
        int restored = find_track_by_path(&list, saved_state.path);
        if (restored >= 0) {
            selected = restored;
        }
        if (saved_state.resume_play) {
            int resume_track = find_track_by_path(&list, saved_state.playing_path);
            if (resume_track >= 0) {
                selected = resume_track;
                playing = resume_selected(&list, selected, saved_state.elapsed_seconds, message, sizeof(message));
                shuffle_history_add(&shuffle_history, playing);
            }
        }
    }
    if (list.count == 0) {
        snprintf(message, sizeof(message), "No MP3 files found");
    } else if (playing < 0) {
        snprintf(message, sizeof(message), "%d tracks found", list.count);
    }

    started_at = SDL_GetTicks();

    while (running) {
        audio_poll();

        /* evdev thread input */
        {
            InputAction action = input_poll_joystick();
            if (handle_action(action, &list, &selected, &playing, &repeat_mode, &running, debug, &shuffle_history, message, sizeof(message))) {
                save_state(state_path, &list, selected, playing, repeat_mode, debug);
                last_state_save = SDL_GetTicks();
            }
        }

        /* SDL event queue (keyboard fallback / SDL_QUIT) */
        while (SDL_PollEvent(&event)) {
            InputAction action = input_event_to_action(&event);
            if (handle_action(action, &list, &selected, &playing, &repeat_mode, &running, debug, &shuffle_history, message, sizeof(message))) {
                save_state(state_path, &list, selected, playing, repeat_mode, debug);
                last_state_save = SDL_GetTicks();
            }
        }

        if (audio_take_finished() && playing >= 0 && list.count > 0) {
            if (repeat_mode == REPEAT_ONE) {
                selected = playing;
                playing = play_selected(&list, selected, message, sizeof(message));
                shuffle_history_add(&shuffle_history, playing);
            } else if (repeat_mode == REPEAT_OFF && playing + 1 >= list.count) {
                playing = -1;
                snprintf(message, sizeof(message), "Finished");
            } else {
                selected = playing + 1;
                clamp_selected(&list, &selected);
                playing = play_selected(&list, selected, message, sizeof(message));
                shuffle_history_add(&shuffle_history, playing);
            }
            save_state(state_path, &list, selected, playing, repeat_mode, debug);
            last_state_save = SDL_GetTicks();
        }

        if (audio_state() == AUDIO_STOPPED) {
            playing = -1;
        }

        if (debug && SDL_GetTicks() - last_log > 10000) {
            printf("Heartbeat selected=%d tracks=%d state=%d\n", selected, list.count, audio_state());
            last_log = SDL_GetTicks();
        }

        if (playing >= 0 && audio_state() != AUDIO_STOPPED && SDL_GetTicks() - last_state_save > 5000) {
            save_state(state_path, &list, selected, playing, repeat_mode, debug);
            last_state_save = SDL_GetTicks();
        }

        if (auto_quit_ms > 0 && SDL_GetTicks() - started_at > auto_quit_ms) {
            printf("Auto quit after %u ms\n", auto_quit_ms);
            running = 0;
        }

        ui_render(&list, selected, playing, audio_state(), audio_elapsed_seconds(), audio_get_volume(), repeat_label(repeat_mode), message);
        SDL_Delay(33);
    }

    save_state(state_path, &list, selected, playing, repeat_mode, debug);
    audio_stop();
    ui_shutdown();
    input_shutdown();
    SDL_Quit();
    return 0;
}
