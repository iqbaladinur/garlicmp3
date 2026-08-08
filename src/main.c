#include "audio_mpg123.h"
#include "file_scan.h"
#include "input.h"
#include "settings.h"
#include "ui_sdl.h"
#include "version.h"

#include <SDL/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RESUME_SKIP_END_SECONDS 10
#define SHUFFLE_RECENT_MAX 8
#define RECENT_MAX 10

typedef struct AppState {
    char path[TRACK_PATH_MAX];
    char playing_path[TRACK_PATH_MAX];
    int volume;
    int resume_play;
    int elapsed_seconds;
    int repeat_mode;
    int debug;
    int favorites_only;
    int eq_preset;
    int eq_bass;
    int eq_mid;
    int eq_treble;
    int rva;
} AppState;

typedef struct AppConfig {
    int repeat_mode;
    int debug;
    int favorites_only;
    int volume_step;
    int eq_preset;
    int eq_bass;
    int eq_mid;
    int eq_treble;
    int rva;
} AppConfig;

typedef struct RecentList {
    char paths[RECENT_MAX][TRACK_PATH_MAX];
    int count;
    int cursor;
} RecentList;

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

enum {
    SCREEN_LIBRARY = 0,
    SCREEN_SETTINGS,
    SCREEN_HELP
};

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

static void favorites_file_path(char *out, size_t out_size, const char *argv0)
{
    const char *slash;

    if (!argv0 || !argv0[0]) {
        snprintf(out, out_size, "favorites.cfg");
        return;
    }

    slash = strrchr(argv0, '/');
    if (!slash) {
        snprintf(out, out_size, "favorites.cfg");
        return;
    }

    snprintf(out, out_size, "%.*s/favorites.cfg", (int)(slash - argv0), argv0);
}

static void recent_file_path(char *out, size_t out_size, const char *argv0)
{
    const char *slash;

    if (!argv0 || !argv0[0]) {
        snprintf(out, out_size, "recent.cfg");
        return;
    }

    slash = strrchr(argv0, '/');
    if (!slash) {
        snprintf(out, out_size, "recent.cfg");
        return;
    }

    snprintf(out, out_size, "%.*s/recent.cfg", (int)(slash - argv0), argv0);
}

static void config_file_path(char *out, size_t out_size, const char *argv0)
{
    const char *slash;

    if (!argv0 || !argv0[0]) {
        snprintf(out, out_size, "config.cfg");
        return;
    }

    slash = strrchr(argv0, '/');
    if (!slash) {
        snprintf(out, out_size, "config.cfg");
        return;
    }

    snprintf(out, out_size, "%.*s/config.cfg", (int)(slash - argv0), argv0);
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

static void load_config(const char *path, AppConfig *config)
{
    FILE *fp;
    char line[128];

    config->repeat_mode = REPEAT_ALL;
    config->debug = 0;
    config->favorites_only = 0;
    config->volume_step = 2;
    config->eq_preset = EQ_PRESET_FLAT;
    config->eq_bass = 10;
    config->eq_mid = 10;
    config->eq_treble = 10;
    config->rva = 0;

    fp = fopen(path, "r");
    if (!fp) {
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        trim_line(line);
        if (strncmp(line, "repeat_mode=", 12) == 0) {
            config->repeat_mode = atoi(line + 12);
            if (config->repeat_mode < REPEAT_OFF || config->repeat_mode > REPEAT_ONE) {
                config->repeat_mode = REPEAT_ALL;
            }
        } else if (strncmp(line, "debug=", 6) == 0) {
            config->debug = atoi(line + 6) ? 1 : 0;
        } else if (strncmp(line, "favorites_only=", 15) == 0) {
            config->favorites_only = atoi(line + 15) ? 1 : 0;
        } else if (strncmp(line, "volume_step=", 12) == 0) {
            config->volume_step = atoi(line + 12);
            if (config->volume_step < 1) {
                config->volume_step = 1;
            } else if (config->volume_step > 20) {
                config->volume_step = 20;
            }
        } else if (strncmp(line, "eq_preset=", 10) == 0) {
            config->eq_preset = atoi(line + 10);
            if (config->eq_preset < 0 || config->eq_preset >= EQ_PRESET_COUNT) {
                config->eq_preset = EQ_PRESET_FLAT;
            }
        } else if (strncmp(line, "eq_bass=", 8) == 0) {
            config->eq_bass = settings_clamp_band(atoi(line + 8));
        } else if (strncmp(line, "eq_mid=", 7) == 0) {
            config->eq_mid = settings_clamp_band(atoi(line + 7));
        } else if (strncmp(line, "eq_treble=", 10) == 0) {
            config->eq_treble = settings_clamp_band(atoi(line + 10));
        } else if (strncmp(line, "rva=", 4) == 0) {
            config->rva = atoi(line + 4) ? 1 : 0;
        }
    }

    fclose(fp);
}

static void load_state(const char *path, AppState *state)
{
    FILE *fp;
    char line[TRACK_PATH_MAX + 64];

    memset(state, 0, sizeof(*state));
    state->volume = -1;
    state->repeat_mode = -1;
    state->debug = -1;
    state->favorites_only = -1;
    state->eq_preset = -1;
    state->eq_bass = -1;
    state->eq_mid = -1;
    state->eq_treble = -1;
    state->rva = -1;

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
        } else if (strncmp(line, "favorites_only=", 15) == 0) {
            state->favorites_only = atoi(line + 15) ? 1 : 0;
        } else if (strncmp(line, "volume=", 7) == 0) {
            state->volume = atoi(line + 7);
        } else if (strncmp(line, "eq_preset=", 10) == 0) {
            state->eq_preset = atoi(line + 10);
            if (state->eq_preset < 0 || state->eq_preset >= EQ_PRESET_COUNT) {
                state->eq_preset = -1;
            }
        } else if (strncmp(line, "eq_bass=", 8) == 0) {
            state->eq_bass = settings_clamp_band(atoi(line + 8));
        } else if (strncmp(line, "eq_mid=", 7) == 0) {
            state->eq_mid = settings_clamp_band(atoi(line + 7));
        } else if (strncmp(line, "eq_treble=", 10) == 0) {
            state->eq_treble = settings_clamp_band(atoi(line + 10));
        } else if (strncmp(line, "rva=", 4) == 0) {
            state->rva = atoi(line + 4) ? 1 : 0;
        }
    }

    fclose(fp);
}

static void save_state(const char *path, const TrackList *list, int selected, int playing, int repeat_mode, int debug, int favorites_only, const Settings *settings)
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
    fprintf(fp, "favorites_only=%d\n", favorites_only ? 1 : 0);
    fprintf(fp, "volume=%d\n", audio_get_volume());
    fprintf(fp, "eq_preset=%d\n", settings->preset);
    fprintf(fp, "eq_bass=%d\n", settings->bass_t);
    fprintf(fp, "eq_mid=%d\n", settings->mid_t);
    fprintf(fp, "eq_treble=%d\n", settings->treble_t);
    fprintf(fp, "rva=%d\n", settings->rva ? 1 : 0);
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

static void load_favorites(const char *path, TrackList *list)
{
    FILE *fp;
    char line[TRACK_PATH_MAX + 8];

    fp = fopen(path, "r");
    if (!fp) {
        return;
    }

    while (fgets(line, sizeof(line), fp)) {
        int idx;
        trim_line(line);
        if (!line[0] || line[0] == '#') {
            continue;
        }
        idx = find_track_by_path(list, line);
        if (idx >= 0) {
            list->tracks[idx].favorite = 1;
        }
    }

    fclose(fp);
}

static void save_favorites(const char *path, const TrackList *list)
{
    FILE *fp;
    int i;

    fp = fopen(path, "w");
    if (!fp) {
        perror("favorites save");
        return;
    }

    for (i = 0; i < list->count; i++) {
        if (list->tracks[i].favorite) {
            fprintf(fp, "%s\n", list->tracks[i].path);
        }
    }

    fclose(fp);
}

static void load_recent(const char *path, const TrackList *list, RecentList *recent)
{
    FILE *fp;
    char line[TRACK_PATH_MAX + 8];

    memset(recent, 0, sizeof(*recent));
    recent->cursor = -1;
    fp = fopen(path, "r");
    if (!fp) {
        return;
    }

    while (recent->count < RECENT_MAX && fgets(line, sizeof(line), fp)) {
        trim_line(line);
        if (!line[0] || line[0] == '#' || find_track_by_path(list, line) < 0) {
            continue;
        }
        snprintf(recent->paths[recent->count], sizeof(recent->paths[recent->count]), "%s", line);
        recent->count++;
    }

    fclose(fp);
}

static void save_recent(const char *path, const RecentList *recent)
{
    FILE *fp;
    int i;

    fp = fopen(path, "w");
    if (!fp) {
        perror("recent save");
        return;
    }

    for (i = 0; i < recent->count; i++) {
        fprintf(fp, "%s\n", recent->paths[i]);
    }

    fclose(fp);
}

static void record_recent(const char *path, RecentList *recent, const char *track_path)
{
    int i;
    int write_count;

    if (!track_path || !track_path[0]) {
        return;
    }

    for (i = 0; i < recent->count; i++) {
        if (strcmp(recent->paths[i], track_path) == 0) {
            int j;
            for (j = i; j > 0; j--) {
                snprintf(recent->paths[j], sizeof(recent->paths[j]), "%s", recent->paths[j - 1]);
            }
            snprintf(recent->paths[0], sizeof(recent->paths[0]), "%s", track_path);
            recent->cursor = -1;
            save_recent(path, recent);
            return;
        }
    }

    write_count = recent->count < RECENT_MAX ? recent->count : RECENT_MAX - 1;
    for (i = write_count; i > 0; i--) {
        snprintf(recent->paths[i], sizeof(recent->paths[i]), "%s", recent->paths[i - 1]);
    }
    snprintf(recent->paths[0], sizeof(recent->paths[0]), "%s", track_path);
    if (recent->count < RECENT_MAX) {
        recent->count++;
    }
    recent->cursor = -1;
    save_recent(path, recent);
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
        snprintf(message, message_size, "%s: %s", audio_last_error(), list->tracks[selected].display_name);
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

    snprintf(message, message_size, "%s", audio_last_error());
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

static int favorite_count(const TrackList *list)
{
    int i;
    int count = 0;

    for (i = 0; i < list->count; i++) {
        if (list->tracks[i].favorite) {
            count++;
        }
    }
    return count;
}

static int track_allowed(const TrackList *list, int idx, int favorites_only)
{
    return idx >= 0 && idx < list->count && (!favorites_only || list->tracks[idx].favorite);
}

static void clamp_selected(const TrackList *list, int *selected);

static void step_selected(const TrackList *list, int *selected, int direction, int favorites_only)
{
    int attempts;

    if (list->count <= 0) {
        *selected = 0;
        return;
    }

    for (attempts = 0; attempts < list->count; attempts++) {
        *selected += direction;
        clamp_selected(list, selected);
        if (track_allowed(list, *selected, favorites_only)) {
            return;
        }
    }
}

static int random_track_index(const TrackList *list, int current, const ShuffleHistory *history, int favorites_only)
{
    int i;
    int next;
    int candidates[TRACK_MAX];
    int candidate_count = 0;

    if (list->count <= 0) {
        return -1;
    }
    if (list->count == 1) {
        return track_allowed(list, 0, favorites_only) ? 0 : -1;
    }

    for (i = 0; i < list->count; i++) {
        if (i != current && track_allowed(list, i, favorites_only) && !shuffle_history_contains(history, i)) {
            candidates[candidate_count++] = i;
        }
    }

    if (candidate_count <= 0) {
        for (i = 0; i < list->count; i++) {
            if (i != current && track_allowed(list, i, favorites_only)) {
                candidates[candidate_count++] = i;
            }
        }
    }

    if (candidate_count <= 0) {
        return track_allowed(list, current, favorites_only) ? current : -1;
    }

    next = rand() % candidate_count;
    return candidates[next];
}

static int jump_folder(const TrackList *list, int selected, int direction)
{
    int i;
    const char *current_folder;

    if (list->count <= 0 || selected < 0 || selected >= list->count) {
        return selected;
    }

    current_folder = list->tracks[selected].folder;
    if (direction > 0) {
        for (i = selected + 1; i < list->count; i++) {
            if (strcmp(list->tracks[i].folder, current_folder) != 0) {
                return i;
            }
        }
        return 0;
    }

    for (i = selected - 1; i >= 0; i--) {
        if (strcmp(list->tracks[i].folder, current_folder) != 0) {
            const char *folder = list->tracks[i].folder;
            while (i > 0 && strcmp(list->tracks[i - 1].folder, folder) == 0) {
                i--;
            }
            return i;
        }
    }

    for (i = list->count - 1; i > 0; i--) {
        if (strcmp(list->tracks[i - 1].folder, list->tracks[i].folder) != 0) {
            return i;
        }
    }
    return 0;
}

static int select_recent_track(const TrackList *list, RecentList *recent, int direction, char *message, size_t message_size)
{
    int attempts;

    if (recent->count <= 0) {
        snprintf(message, message_size, "No recent tracks");
        return -1;
    }

    if (recent->cursor < 0 || recent->cursor >= recent->count) {
        recent->cursor = direction > 0 ? 0 : recent->count - 1;
    } else {
        recent->cursor += direction > 0 ? 1 : -1;
        if (recent->cursor < 0) {
            recent->cursor = recent->count - 1;
        } else if (recent->cursor >= recent->count) {
            recent->cursor = 0;
        }
    }

    for (attempts = 0; attempts < recent->count; attempts++) {
        int idx = find_track_by_path(list, recent->paths[recent->cursor]);
        if (idx >= 0) {
            snprintf(message, message_size, "Recent: %s", list->tracks[idx].display_name);
            return idx;
        }
        recent->cursor += direction > 0 ? 1 : -1;
        if (recent->cursor < 0) {
            recent->cursor = recent->count - 1;
        } else if (recent->cursor >= recent->count) {
            recent->cursor = 0;
        }
    }

    snprintf(message, message_size, "No recent tracks");
    return -1;
}

static int auto_advance_track(const TrackList *list, int *selected, int *playing, int repeat_mode, int favorites_only, ShuffleHistory *shuffle_history, char *message, size_t message_size)
{
    int attempts;
    int next;

    if (list->count <= 0 || *playing < 0) {
        *playing = -1;
        return 0;
    }

    if (repeat_mode == REPEAT_ONE) {
        *selected = *playing;
        *playing = play_selected(list, *selected, message, message_size);
        shuffle_history_add(shuffle_history, *playing);
        return *playing >= 0;
    }

    next = *playing + 1;
    for (attempts = 0; attempts < list->count; attempts++) {
        if (next >= list->count) {
            if (repeat_mode == REPEAT_OFF) {
                *playing = -1;
                snprintf(message, message_size, "Finished");
                return 0;
            }
            next = 0;
        }

        if (!track_allowed(list, next, favorites_only)) {
            next++;
            continue;
        }
        *selected = next;
        *playing = play_selected(list, *selected, message, message_size);
        shuffle_history_add(shuffle_history, *playing);
        if (*playing >= 0) {
            if (attempts > 0) {
                snprintf(message, message_size, "Skipped bad track");
            }
            return 1;
        }
        next++;
    }

    *playing = -1;
    snprintf(message, message_size, "No playable tracks");
    return 0;
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

static int handle_action(InputAction action, TrackList *list, int *selected, int *playing, int *repeat_mode, int *favorites_only, int *running, int debug, ShuffleHistory *shuffle_history, RecentList *recent, const char *favorites_path, const char *recent_path, char *message, size_t message_size)
{
    int save_needed = 0;

    if (debug && action != ACTION_NONE) {
        printf("Action=%d selected=%d tracks=%d\n", action, *selected, list->count);
    }

    switch (action) {
    case ACTION_UP:
        step_selected(list, selected, -1, *favorites_only);
        if (list->count <= 0) {
            snprintf(message, message_size, "No MP3 files found");
        }
        save_needed = 1;
        break;
    case ACTION_DOWN:
        step_selected(list, selected, 1, *favorites_only);
        if (list->count <= 0) {
            snprintf(message, message_size, "No MP3 files found");
        }
        save_needed = 1;
        break;
    case ACTION_PLAY:
        if (!track_allowed(list, *selected, *favorites_only)) {
            snprintf(message, message_size, "Not in favorites filter");
        } else {
            *playing = play_selected(list, *selected, message, message_size);
            shuffle_history_add(shuffle_history, *playing);
            if (*playing >= 0) {
                record_recent(recent_path, recent, list->tracks[*playing].path);
            }
            save_needed = *playing >= 0;
        }
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
    case ACTION_FAVORITE_TOGGLE:
        if (list->count > 0 && *selected >= 0 && *selected < list->count) {
            list->tracks[*selected].favorite = !list->tracks[*selected].favorite;
            save_favorites(favorites_path, list);
            snprintf(message, message_size, "%s favorite: %s",
                     list->tracks[*selected].favorite ? "Added" : "Removed",
                     list->tracks[*selected].display_name);
        } else {
            snprintf(message, message_size, "No track selected");
        }
        break;
    case ACTION_FAVORITES_ONLY_TOGGLE:
        *favorites_only = !*favorites_only;
        if (*favorites_only && favorite_count(list) <= 0) {
            *favorites_only = 0;
            snprintf(message, message_size, "No favorites yet");
        } else {
            if (*favorites_only && !track_allowed(list, *selected, 1)) {
                int i;
                for (i = 0; i < list->count; i++) {
                    if (list->tracks[i].favorite) {
                        *selected = i;
                        break;
                    }
                }
            }
            snprintf(message, message_size, "%s", *favorites_only ? "Favorites only" : "All tracks");
            save_needed = 1;
        }
        break;
    case ACTION_SHUFFLE_PLAY:
        if (list->count > 0) {
            int shuffle_favorites = *favorites_only || (*selected >= 0 && *selected < list->count && list->tracks[*selected].favorite);
            int next = random_track_index(list, *playing >= 0 ? *playing : *selected, shuffle_history, shuffle_favorites);
            if (next >= 0) {
                *selected = next;
                *playing = play_selected(list, *selected, message, message_size);
                shuffle_history_add(shuffle_history, *playing);
                if (*playing >= 0) {
                    record_recent(recent_path, recent, list->tracks[*playing].path);
                }
                save_needed = *playing >= 0;
            } else if (shuffle_favorites) {
                snprintf(message, message_size, "No favorites to shuffle");
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
            step_selected(list, selected, -1, *favorites_only);
            *playing = play_selected(list, *selected, message, message_size);
            shuffle_history_add(shuffle_history, *playing);
            if (*playing >= 0) {
                record_recent(recent_path, recent, list->tracks[*playing].path);
            }
            save_needed = 1;
        }
        break;
    case ACTION_NEXT:
        if (list->count > 0) {
            step_selected(list, selected, 1, *favorites_only);
            *playing = play_selected(list, *selected, message, message_size);
            shuffle_history_add(shuffle_history, *playing);
            if (*playing >= 0) {
                record_recent(recent_path, recent, list->tracks[*playing].path);
            }
            save_needed = 1;
        }
        break;
    case ACTION_FOLDER_PREV:
        if (list->count > 0) {
            *selected = jump_folder(list, *selected, -1);
            if (!track_allowed(list, *selected, *favorites_only)) {
                step_selected(list, selected, -1, *favorites_only);
            }
            snprintf(message, message_size, "Folder: %s", list->tracks[*selected].folder);
            save_needed = 1;
        }
        break;
    case ACTION_FOLDER_NEXT:
        if (list->count > 0) {
            *selected = jump_folder(list, *selected, 1);
            if (!track_allowed(list, *selected, *favorites_only)) {
                step_selected(list, selected, 1, *favorites_only);
            }
            snprintf(message, message_size, "Folder: %s", list->tracks[*selected].folder);
            save_needed = 1;
        }
        break;
    case ACTION_RECENT_PREV:
        {
            int recent_idx = select_recent_track(list, recent, -1, message, message_size);
            if (recent_idx >= 0) {
                *selected = recent_idx;
                save_needed = 1;
            }
        }
        break;
    case ACTION_RECENT_NEXT:
        {
            int recent_idx = select_recent_track(list, recent, 1, message, message_size);
            if (recent_idx >= 0) {
                *selected = recent_idx;
                save_needed = 1;
            }
        }
        break;
    case ACTION_VOL_DOWN:
        audio_volume_down();
        snprintf(message, message_size, "%s", audio_last_error()[0] ? audio_last_error() : "Volume down");
        save_needed = 1;
        break;
    case ACTION_VOL_UP:
        audio_volume_up();
        snprintf(message, message_size, "%s", audio_last_error()[0] ? audio_last_error() : "Volume up");
        save_needed = 1;
        break;
    case ACTION_QUIT:
        *running = 0;
        save_needed = 1;
        break;
    default:
        break;
    }

    return save_needed;
}

static void settings_value_message(const Settings *s, char *buf, size_t n)
{
    switch (s->cursor) {
    case SETTINGS_ITEM_REPEAT:
        snprintf(buf, n, "Repeat: %s", repeat_label(s->repeat_mode));
        break;
    case SETTINGS_ITEM_FAVORITES_ONLY:
        snprintf(buf, n, "Favorites only: %s", s->favorites_only ? "On" : "Off");
        break;
    case SETTINGS_ITEM_VOLUME_STEP:
        snprintf(buf, n, "Volume step: %d", s->volume_step);
        break;
    case SETTINGS_ITEM_PRESET:
        snprintf(buf, n, "EQ: %s", settings_preset_name(s->preset));
        break;
    case SETTINGS_ITEM_BASS:
        snprintf(buf, n, "Bass: %d.%d", s->bass_t / 10, s->bass_t % 10);
        break;
    case SETTINGS_ITEM_MID:
        snprintf(buf, n, "Mid: %d.%d", s->mid_t / 10, s->mid_t % 10);
        break;
    case SETTINGS_ITEM_TREBLE:
        snprintf(buf, n, "Treble: %d.%d", s->treble_t / 10, s->treble_t % 10);
        break;
    case SETTINGS_ITEM_RVA:
        snprintf(buf, n, "Normalize (RVA): %s", s->rva ? "On" : "Off");
        break;
    case SETTINGS_ITEM_DEBUG:
        snprintf(buf, n, "Debug logging: %s", s->debug ? "On" : "Off");
        break;
    default:
        buf[0] = '\0';
        break;
    }
}

static int handle_settings_action(InputAction action, Settings *settings, int *screen, int *repeat_mode, int *favorites_only, int *debug, char *message, size_t message_size)
{
    switch (action) {
    case ACTION_UP:
        settings_cursor_move(settings, -1);
        return 0;
    case ACTION_DOWN:
        settings_cursor_move(settings, 1);
        return 0;
    case ACTION_PREV:
    case ACTION_NEXT:
        if (settings_adjust(settings, action == ACTION_NEXT ? 1 : -1)) {
            audio_set_eq(settings->bass_t, settings->mid_t, settings->treble_t);
            audio_set_rva(settings->rva);
            audio_set_volume_step(settings->volume_step);
            *repeat_mode = settings->repeat_mode;
            *favorites_only = settings->favorites_only;
            *debug = settings->debug;
            settings_value_message(settings, message, message_size);
            return 1;
        }
        return 0;
    case ACTION_STOP: /* B closes the menu */
        *screen = SCREEN_LIBRARY;
        return 0;
    case ACTION_PAUSE:
    case ACTION_VOL_DOWN:
    case ACTION_VOL_UP:
    case ACTION_QUIT:
        return -1; /* pass through to the library handler */
    default:
        return 0; /* consume everything else while the menu is open */
    }
}

static int dispatch_action(InputAction action, int *screen, Settings *settings, TrackList *list, int *selected, int *playing, int *repeat_mode, int *favorites_only, int *running, int *debug, ShuffleHistory *shuffle_history, RecentList *recent, const char *favorites_path, const char *recent_path, char *message, size_t message_size)
{
    if (action == ACTION_SETTINGS_TOGGLE) {
        *screen = *screen == SCREEN_SETTINGS ? SCREEN_LIBRARY : SCREEN_SETTINGS;
        message[0] = '\0';
        return 0;
    }
    if (action == ACTION_HELP_TOGGLE) {
        *screen = *screen == SCREEN_HELP ? SCREEN_LIBRARY : SCREEN_HELP;
        message[0] = '\0';
        return 0;
    }
    if (*screen == SCREEN_SETTINGS) {
        int handled = handle_settings_action(action, settings, screen, repeat_mode, favorites_only, debug, message, message_size);
        if (handled >= 0) {
            if (*favorites_only && favorite_count(list) <= 0) {
                *favorites_only = 0;
                settings->favorites_only = 0;
                snprintf(message, message_size, "No favorites yet");
            }
            return handled;
        }
    }
    if (*screen == SCREEN_HELP) {
        if (action == ACTION_STOP || action == ACTION_SETTINGS_TOGGLE || action == ACTION_HELP_TOGGLE) {
            *screen = SCREEN_LIBRARY;
            message[0] = '\0';
            return 0;
        }
        return 0; /* consume everything else while help is open */
    }
    return handle_action(action, list, selected, playing, repeat_mode, favorites_only, running, *debug, shuffle_history, recent, favorites_path, recent_path, message, message_size);
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
    int favorites_only = 0;
    int screen = SCREEN_LIBRARY;
    Settings settings;
    Uint32 last_log = 0;
    Uint32 last_state_save = 0;
    Uint32 started_at = 0;
    Uint32 auto_quit_ms = 0;
    char message[128] = "";
    char state_path[TRACK_PATH_MAX];
    char favorites_path[TRACK_PATH_MAX];
    char recent_path[TRACK_PATH_MAX];
    char config_path[TRACK_PATH_MAX];
    AppState saved_state;
    AppConfig config;
    RecentList recent;
    ShuffleHistory shuffle_history;

    (void)argc;

    memset(&shuffle_history, 0, sizeof(shuffle_history));
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("main start\n");
    printf("GarlicMP3 version %s (git %s)\n", GARLICMP3_VERSION, GARLICMP3_GIT_HASH);
    srand((unsigned int)time(NULL));
    state_file_path(state_path, sizeof(state_path), argv && argv[0] ? argv[0] : NULL);
    favorites_file_path(favorites_path, sizeof(favorites_path), argv && argv[0] ? argv[0] : NULL);
    recent_file_path(recent_path, sizeof(recent_path), argv && argv[0] ? argv[0] : NULL);
    config_file_path(config_path, sizeof(config_path), argv && argv[0] ? argv[0] : NULL);
    load_config(config_path, &config);
    load_state(state_path, &saved_state);
    repeat_mode = saved_state.repeat_mode >= 0 ? saved_state.repeat_mode : config.repeat_mode;
    debug = saved_state.debug >= 0 ? saved_state.debug : config.debug;
    favorites_only = saved_state.favorites_only >= 0 ? saved_state.favorites_only : config.favorites_only;
    settings_init(&settings);
    settings_set_preset(&settings, saved_state.eq_preset >= 0 ? saved_state.eq_preset : config.eq_preset);
    if (settings.preset == EQ_PRESET_CUSTOM) {
        settings.bass_t = saved_state.eq_bass >= 0 ? saved_state.eq_bass : config.eq_bass;
        settings.mid_t = saved_state.eq_mid >= 0 ? saved_state.eq_mid : config.eq_mid;
        settings.treble_t = saved_state.eq_treble >= 0 ? saved_state.eq_treble : config.eq_treble;
    }
    settings.rva = saved_state.rva >= 0 ? saved_state.rva : config.rva;
    settings.repeat_mode = repeat_mode;
    settings.favorites_only = favorites_only;
    settings.volume_step = config.volume_step;
    settings.debug = debug;
    audio_set_volume_step(settings.volume_step);
    printf("state file: %s favorites file: %s recent file: %s config file: %s saved_volume=%d debug=%d favorites_only=%d volume_step=%d\n",
           state_path, favorites_path, recent_path, config_path, saved_state.volume, debug, favorites_only, settings.volume_step);

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

    if (audio_init() != 0) {
        snprintf(message, sizeof(message), "%s", audio_last_error());
    }
    audio_set_eq(settings.bass_t, settings.mid_t, settings.treble_t);
    audio_set_rva(settings.rva);

    scan_music(&list, argv && argv[0] ? argv[0] : NULL);
    load_favorites(favorites_path, &list);
    load_recent(recent_path, &list, &recent);
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
        if (favorites_only && favorite_count(&list) <= 0) {
            favorites_only = 0;
        }
        snprintf(message, sizeof(message), "%d tracks found", list.count);
    }

    started_at = SDL_GetTicks();

    while (running) {
        audio_poll();

        /* evdev thread input */
        {
            InputAction action = input_poll_joystick();
            if (dispatch_action(action, &screen, &settings, &list, &selected, &playing, &repeat_mode, &favorites_only, &running, &debug, &shuffle_history, &recent, favorites_path, recent_path, message, sizeof(message))) {
                save_state(state_path, &list, selected, playing, repeat_mode, debug, favorites_only, &settings);
                last_state_save = SDL_GetTicks();
            }
        }

        /* SDL event queue (keyboard fallback / SDL_QUIT) */
        while (SDL_PollEvent(&event)) {
            InputAction action = input_event_to_action(&event);
            if (dispatch_action(action, &screen, &settings, &list, &selected, &playing, &repeat_mode, &favorites_only, &running, &debug, &shuffle_history, &recent, favorites_path, recent_path, message, sizeof(message))) {
                save_state(state_path, &list, selected, playing, repeat_mode, debug, favorites_only, &settings);
                last_state_save = SDL_GetTicks();
            }
        }

        if (audio_take_finished() && playing >= 0 && list.count > 0) {
            auto_advance_track(&list, &selected, &playing, repeat_mode, favorites_only, &shuffle_history, message, sizeof(message));
            if (playing >= 0) {
                record_recent(recent_path, &recent, list.tracks[playing].path);
            }
            save_state(state_path, &list, selected, playing, repeat_mode, debug, favorites_only, &settings);
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
            save_state(state_path, &list, selected, playing, repeat_mode, debug, favorites_only, &settings);
            last_state_save = SDL_GetTicks();
        }

        if (auto_quit_ms > 0 && SDL_GetTicks() - started_at > auto_quit_ms) {
            printf("Auto quit after %u ms\n", auto_quit_ms);
            running = 0;
        }

        if (screen == SCREEN_SETTINGS) {
            ui_render_settings(&settings, audio_state(), message);
        } else if (screen == SCREEN_HELP) {
            ui_render_help(audio_state());
        } else {
            ui_render(&list, selected, playing, audio_state(), audio_elapsed_seconds(), audio_duration_seconds(), audio_get_volume(), repeat_label(repeat_mode), settings_preset_name(settings.preset), favorites_only, message);
        }
        SDL_Delay(33);
    }

    save_state(state_path, &list, selected, playing, repeat_mode, debug, favorites_only, &settings);
    audio_shutdown();
    ui_shutdown();
    input_shutdown();
    SDL_Quit();
    return 0;
}
