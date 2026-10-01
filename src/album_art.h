#ifndef ALBUM_ART_H
#define ALBUM_ART_H

/* Loads album art and decodes it to RGBA via stb_image: the embedded picture
 * (MP3: ID3v2 APIC frame, FLAC: PICTURE block), else a cover image next to
 * the track (cover/folder/front .jpg/.png). Returns 1 on success — caller
 * must free() *out_rgba — or 0 when there is no usable art. Pure C, no SDL
 * dependency. */
int album_art_load(const char *path, unsigned char **out_rgba,
                   int *out_w, int *out_h);

#endif
