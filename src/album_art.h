#ifndef ALBUM_ART_H
#define ALBUM_ART_H

/* Loads embedded album art (ID3v2 APIC frame) from an MP3 file and decodes it
 * to RGBA via stb_image. Returns 1 on success — caller must free() *out_rgba —
 * or 0 when the file has no usable art. Pure C, no SDL dependency. */
int album_art_load(const char *mp3_path, unsigned char **out_rgba,
                   int *out_w, int *out_h);

#endif
