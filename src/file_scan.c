#define _GNU_SOURCE
#include "file_scan.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int has_mp3_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return 0;
    }
    return strcasecmp(dot, ".mp3") == 0;
}

static int is_regular_or_unknown(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    return S_ISREG(st.st_mode);
}

static void add_track(TrackList *list, const char *dir, const char *name)
{
    Track *track;

    if (!has_mp3_ext(name)) {
        return;
    }

    if (list->count >= TRACK_MAX) {
        list->truncated = 1;
        return;
    }

    track = &list->tracks[list->count];
    snprintf(track->path, sizeof(track->path), "%s/%s", dir, name);
    if (!is_regular_or_unknown(track->path)) {
        return;
    }
    snprintf(track->name, sizeof(track->name), "%s", name);
    list->count++;
}

static void scan_dir_depth(TrackList *list, const char *dir, int depth)
{
    DIR *dp;
    struct dirent *ent;

    dp = opendir(dir);
    if (!dp) {
        return;
    }

    while ((ent = readdir(dp)) != NULL) {
        char path[TRACK_PATH_MAX];
        struct stat st;

        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (stat(path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode) && depth > 0) {
            scan_dir_depth(list, path, depth - 1);
        } else if (S_ISREG(st.st_mode)) {
            add_track(list, dir, ent->d_name);
        }
    }

    closedir(dp);
}

static int cmp_track(const void *a, const void *b)
{
    const Track *ta = (const Track *)a;
    const Track *tb = (const Track *)b;
    return strcasecmp(ta->name, tb->name);
}

static void app_music_dir(char *out, size_t out_size, const char *argv0)
{
    const char *slash;

    if (!argv0 || !argv0[0]) {
        snprintf(out, out_size, "./MUSIC");
        return;
    }

    slash = strrchr(argv0, '/');
    if (!slash) {
        snprintf(out, out_size, "./MUSIC");
        return;
    }

    snprintf(out, out_size, "%.*s/MUSIC", (int)(slash - argv0), argv0);
}

static void sibling_roms_music_dir(char *out, size_t out_size, const char *argv0)
{
    const char *slash;

    if (!argv0 || !argv0[0]) {
        snprintf(out, out_size, "../../MUSIC");
        return;
    }

    slash = strrchr(argv0, '/');
    if (!slash) {
        snprintf(out, out_size, "../../MUSIC");
        return;
    }

    snprintf(out, out_size, "%.*s/../../MUSIC", (int)(slash - argv0), argv0);
}

static int path_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

void scan_music(TrackList *list, const char *argv0)
{
    char local_app_dir[TRACK_PATH_MAX];
    char sibling_music_dir[TRACK_PATH_MAX];
    const char *dirs[4];
    int i;

    memset(list, 0, sizeof(*list));
    app_music_dir(local_app_dir, sizeof(local_app_dir), argv0);
    sibling_roms_music_dir(sibling_music_dir, sizeof(sibling_music_dir), argv0);

    dirs[0] = sibling_music_dir;
    dirs[1] = "/mnt/mmc/MUSIC";
    dirs[2] = "./MUSIC";
    dirs[3] = local_app_dir;

    for (i = 0; i < 4; i++) {
        if (i > 0 && strcmp(dirs[i], dirs[i - 1]) == 0) {
            continue;
        }
        if (path_exists(dirs[i])) {
            scan_dir_depth(list, dirs[i], 1);
        }
    }

    qsort(list->tracks, list->count, sizeof(list->tracks[0]), cmp_track);
}
