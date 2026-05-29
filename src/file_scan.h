#ifndef FILE_SCAN_H
#define FILE_SCAN_H

#define TRACK_MAX 512
#define TRACK_PATH_MAX 1024
#define TRACK_NAME_MAX 256

typedef struct Track {
    char path[TRACK_PATH_MAX];
    char name[TRACK_NAME_MAX];
    char display_name[TRACK_NAME_MAX];
    int duration_seconds;
} Track;

typedef struct TrackList {
    Track tracks[TRACK_MAX];
    int count;
    int truncated;
} TrackList;

void scan_music(TrackList *list, const char *argv0);

#endif
