/* Host test: scan a MUSIC dir with Japanese tags/filenames and print the
 * display names as UTF-8 so we can verify file_scan decoding. */
#include <stdio.h>
#include <string.h>

#include "file_scan.h"

int main(int argc, char **argv)
{
    TrackList list;
    int i;

    scan_music(&list, argc > 1 ? argv[1] : NULL);
    printf("tracks=%d\n", list.count);
    for (i = 0; i < list.count; i++) {
        printf("[%02d] %s  (folder=%s)\n", i, list.tracks[i].display_name, list.tracks[i].folder);
    }
    return 0;
}
