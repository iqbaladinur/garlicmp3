#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
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

static unsigned int syncsafe_u32(const unsigned char *p)
{
    return ((unsigned int)(p[0] & 0x7f) << 21) |
           ((unsigned int)(p[1] & 0x7f) << 14) |
           ((unsigned int)(p[2] & 0x7f) << 7) |
           (unsigned int)(p[3] & 0x7f);
}

static int bitrate_kbps(int version_id, int layer_id, int bitrate_index)
{
    static const int mpeg1_l3[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static const int mpeg2_l3[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};

    if (layer_id != 1 || bitrate_index <= 0 || bitrate_index >= 15) {
        return 0;
    }

    return version_id == 3 ? mpeg1_l3[bitrate_index] : mpeg2_l3[bitrate_index];
}

static int sample_rate_hz(int version_id, int sample_index)
{
    static const int base[3] = {44100, 48000, 32000};
    int rate;

    if (sample_index < 0 || sample_index >= 3) {
        return 0;
    }

    rate = base[sample_index];
    if (version_id == 2) {
        rate /= 2;
    } else if (version_id == 0) {
        rate /= 4;
    } else if (version_id != 3) {
        return 0;
    }
    return rate;
}

static int read_u32_be(FILE *fp, long offset, unsigned int *out)
{
    unsigned char b[4];

    if (fseek(fp, offset, SEEK_SET) != 0) {
        return 0;
    }
    if (fread(b, 1, sizeof(b), fp) != sizeof(b)) {
        return 0;
    }
    *out = ((unsigned int)b[0] << 24) | ((unsigned int)b[1] << 16) | ((unsigned int)b[2] << 8) | (unsigned int)b[3];
    return 1;
}

static int xing_duration(FILE *fp, long frame_offset, int version_id, int channel_mode, int sample_rate)
{
    long xing_offset;
    unsigned char tag[4];
    unsigned int flags;
    unsigned int frames;
    int samples_per_frame;

    if (version_id == 3) {
        xing_offset = frame_offset + 4 + (channel_mode == 3 ? 17 : 32);
        samples_per_frame = 1152;
    } else {
        xing_offset = frame_offset + 4 + (channel_mode == 3 ? 9 : 17);
        samples_per_frame = 576;
    }

    if (fseek(fp, xing_offset, SEEK_SET) != 0 || fread(tag, 1, sizeof(tag), fp) != sizeof(tag)) {
        return 0;
    }
    if (memcmp(tag, "Xing", 4) != 0 && memcmp(tag, "Info", 4) != 0) {
        return 0;
    }
    if (!read_u32_be(fp, xing_offset + 4, &flags) || !(flags & 1)) {
        return 0;
    }
    if (!read_u32_be(fp, xing_offset + 8, &frames) || frames == 0 || sample_rate <= 0) {
        return 0;
    }

    return (int)(((unsigned long long)frames * (unsigned int)samples_per_frame + (unsigned int)sample_rate / 2) / (unsigned int)sample_rate);
}

static int estimate_mp3_duration(const char *path)
{
    FILE *fp;
    unsigned char h[10];
    long file_size;
    long offset = 0;
    long search_end;
    int duration = 0;

    fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return 0;
    }
    file_size = ftell(fp);
    if (file_size <= 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }

    if (fread(h, 1, sizeof(h), fp) == sizeof(h) && memcmp(h, "ID3", 3) == 0) {
        offset = 10 + (long)syncsafe_u32(&h[6]);
    }

    search_end = offset + 65536;
    if (search_end > file_size - 4) {
        search_end = file_size - 4;
    }

    while (offset < search_end) {
        unsigned int header;
        int version_id;
        int layer_id;
        int bitrate_index;
        int sample_index;
        int channel_mode;
        int bitrate;
        int sample_rate;

        if (!read_u32_be(fp, offset, &header)) {
            break;
        }

        if ((header & 0xffe00000u) == 0xffe00000u) {
            version_id = (int)((header >> 19) & 3);
            layer_id = (int)((header >> 17) & 3);
            bitrate_index = (int)((header >> 12) & 15);
            sample_index = (int)((header >> 10) & 3);
            channel_mode = (int)((header >> 6) & 3);
            bitrate = bitrate_kbps(version_id, layer_id, bitrate_index);
            sample_rate = sample_rate_hz(version_id, sample_index);

            if (bitrate > 0 && sample_rate > 0) {
                duration = xing_duration(fp, offset, version_id, channel_mode, sample_rate);
                if (duration <= 0) {
                    long audio_bytes = file_size - offset;
                    if (audio_bytes > 128) {
                        duration = (int)(((unsigned long long)audio_bytes * 8u + (unsigned int)bitrate * 500u) / ((unsigned int)bitrate * 1000u));
                    }
                }
                break;
            }
        }

        offset++;
    }

    fclose(fp);
    return duration > 0 ? duration : 0;
}

static void clean_display_name(char *out, size_t out_size, const char *name)
{
    size_t i;
    size_t j = 0;
    size_t len;
    size_t end;
    int last_space = 1;

    if (!out || out_size == 0) {
        return;
    }

    out[0] = '\0';
    if (!name) {
        return;
    }

    len = strlen(name);
    end = len;
    if (len > 4 && strcasecmp(name + len - 4, ".mp3") == 0) {
        end = len - 4;
    }

    for (i = 0; i < end && j + 1 < out_size; i++) {
        unsigned char c = (unsigned char)name[i];

        if (c == '_' || c == '-' || c == '.') {
            c = ' ';
        }

        if (isspace(c)) {
            if (!last_space && j + 1 < out_size) {
                out[j++] = ' ';
            }
            last_space = 1;
            continue;
        }

        out[j++] = (char)c;
        last_space = 0;
    }

    while (j > 0 && out[j - 1] == ' ') {
        j--;
    }
    out[j] = '\0';

    if (out[0] == '\0') {
        snprintf(out, out_size, "%s", name);
    }
}

static void copy_id3_text(char *out, size_t out_size, const unsigned char *src, size_t src_size)
{
    size_t start = 0;
    size_t end = src_size;
    size_t i;
    size_t j = 0;
    int last_space = 1;

    if (!out || out_size == 0) {
        return;
    }
    out[0] = '\0';

    while (start < src_size && (src[start] == '\0' || isspace(src[start]))) {
        start++;
    }
    while (end > start && (src[end - 1] == '\0' || isspace(src[end - 1]))) {
        end--;
    }

    for (i = start; i < end && j + 1 < out_size; i++) {
        unsigned char c = src[i];
        if (c < 32 || c > 126) {
            c = ' ';
        }
        if (isspace(c)) {
            if (!last_space && j + 1 < out_size) {
                out[j++] = ' ';
            }
            last_space = 1;
        } else {
            out[j++] = (char)c;
            last_space = 0;
        }
    }
    out[j] = '\0';
}

static int read_id3v1_display_name(const char *path, char *out, size_t out_size)
{
    FILE *fp;
    unsigned char tag[128];
    char title[64];
    char artist[64];

    if (!out || out_size == 0) {
        return 0;
    }
    out[0] = '\0';

    fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    if (fseek(fp, -128, SEEK_END) != 0 || fread(tag, 1, sizeof(tag), fp) != sizeof(tag)) {
        fclose(fp);
        return 0;
    }
    fclose(fp);

    if (memcmp(tag, "TAG", 3) != 0) {
        return 0;
    }

    copy_id3_text(title, sizeof(title), tag + 3, 30);
    copy_id3_text(artist, sizeof(artist), tag + 33, 30);

    if (title[0] && artist[0]) {
        snprintf(out, out_size, "%s - %s", artist, title);
    } else if (title[0]) {
        snprintf(out, out_size, "%s", title);
    } else {
        return 0;
    }

    return 1;
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
    if (!read_id3v1_display_name(track->path, track->display_name, sizeof(track->display_name))) {
        clean_display_name(track->display_name, sizeof(track->display_name), name);
    }
    track->duration_seconds = estimate_mp3_duration(track->path);
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
