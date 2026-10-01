#include "album_art.h"

#include "flac_meta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb/stb_image.h"

/* Cap the ID3v2 tag we read — covers sit right after the tag header, so the
 * whole tag (header + frames) never needs to exceed this. */
#define ALBUM_ART_TAG_CAP (16u * 1024u * 1024u)

static unsigned long syncsafe_u32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 21) | ((unsigned long)p[1] << 14) |
           ((unsigned long)p[2] << 7) | (unsigned long)p[3];
}

static unsigned long normal_u32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

/* Point to the payload of the APIC frame inside a full ID3v2 tag buffer.
 * Returns NULL when there is no APIC frame. */
static const unsigned char *find_apic(const unsigned char *tag,
                                      unsigned long tag_size,
                                      unsigned long *apic_len)
{
    unsigned long offset;
    int version;

    if (tag_size < 10 || memcmp(tag, "ID3", 3) != 0) {
        return NULL;
    }
    version = tag[3];
    if (version < 3 || version > 4) {
        return NULL;
    }

    offset = 10;
    /* Skip the extended header if flagged (rare; best-effort). */
    if (tag[5] & 0x40) {
        unsigned long ext;
        if (offset + 4 > tag_size) {
            return NULL;
        }
        ext = version == 4 ? syncsafe_u32(tag + offset) : normal_u32(tag + offset);
        offset += 4 + ext;
    }

    while (offset + 10 <= tag_size) {
        const unsigned char *fh = tag + offset;
        unsigned long fsize;

        if (fh[0] == 0) {
            break; /* padding */
        }
        fsize = version == 4 ? syncsafe_u32(fh + 4) : normal_u32(fh + 4);
        if (offset + 10 + fsize > tag_size) {
            break;
        }
        if (memcmp(fh, "APIC", 4) == 0) {
            *apic_len = fsize;
            return fh + 10;
        }
        offset += 10 + fsize;
    }
    return NULL;
}

/* Peel the APIC payload down to the raw encoded image bytes (after MIME type,
 * picture type and description). Returns 1 on success. */
static int extract_image(const unsigned char *apic, unsigned long apic_len,
                         const unsigned char **img, unsigned long *img_len)
{
    unsigned long off;
    int enc;

    if (apic_len < 4) {
        return 0;
    }
    enc = apic[0];
    off = 1;

    /* MIME type: ISO-8859-1, null-terminated. */
    while (off < apic_len && apic[off] != 0) {
        off++;
    }
    if (off >= apic_len) {
        return 0;
    }
    off++; /* null */
    if (off >= apic_len) {
        return 0;
    }
    off++; /* picture type (3 = front cover) */

    /* Description: null-terminated; UTF-16 uses a 2-byte terminator. */
    if (enc == 1 || enc == 2) {
        while (off + 1 < apic_len && !(apic[off] == 0 && apic[off + 1] == 0)) {
            off += 2;
        }
        off += 2;
    } else {
        while (off < apic_len && apic[off] != 0) {
            off++;
        }
        off++;
    }
    if (off >= apic_len) {
        return 0;
    }

    *img = apic + off;
    *img_len = apic_len - off;
    return 1;
}

static int load_id3_art(const char *mp3_path, unsigned char **out_rgba,
                        int *out_w, int *out_h)
{
    FILE *fp;
    unsigned char hdr[10];
    unsigned char *tag = NULL;
    unsigned long tag_size = 0;
    const unsigned char *apic = NULL;
    unsigned long apic_len = 0;
    const unsigned char *img = NULL;
    unsigned long img_len = 0;
    int ok = 0;

    if (!out_rgba || !out_w || !out_h) {
        return 0;
    }
    *out_rgba = NULL;
    *out_w = 0;
    *out_h = 0;

    fp = fopen(mp3_path, "rb");
    if (!fp) {
        return 0;
    }
    if (fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr) ||
        memcmp(hdr, "ID3", 3) != 0) {
        fclose(fp);
        return 0;
    }

    tag_size = 10 + syncsafe_u32(&hdr[6]);
    if (tag_size > ALBUM_ART_TAG_CAP) {
        tag_size = ALBUM_ART_TAG_CAP;
    }
    tag = (unsigned char *)malloc(tag_size);
    if (!tag) {
        fclose(fp);
        return 0;
    }
    memcpy(tag, hdr, sizeof(hdr));
    if (fread(tag + 10, 1, tag_size - 10, fp) != tag_size - 10) {
        free(tag);
        fclose(fp);
        return 0;
    }
    fclose(fp);

    apic = find_apic(tag, tag_size, &apic_len);
    if (apic && apic_len > 0 &&
        extract_image(apic, apic_len, &img, &img_len) && img_len > 0) {
        int w, h, n;
        unsigned char *rgba = stbi_load_from_memory(img, (int)img_len, &w, &h, &n, 4);
        if (rgba && w > 0 && h > 0) {
            *out_rgba = rgba;
            *out_w = w;
            *out_h = h;
            ok = 1;
        }
    }

    free(tag);
    return ok;
}

static int decode_image(const unsigned char *img, unsigned long img_len,
                        unsigned char **out_rgba, int *out_w, int *out_h)
{
    int w, h, n;
    unsigned char *rgba;

    if (!img || img_len == 0 || img_len > 0x7fffffffUL) {
        return 0;
    }
    rgba = stbi_load_from_memory(img, (int)img_len, &w, &h, &n, 4);
    if (!rgba || w <= 0 || h <= 0) {
        stbi_image_free(rgba);
        return 0;
    }
    *out_rgba = rgba;
    *out_w = w;
    *out_h = h;
    return 1;
}

static int load_flac_art(const char *path, unsigned char **out_rgba, int *out_w, int *out_h)
{
    unsigned char *img = NULL;
    unsigned long len = 0;
    int ok = 0;

    if (flac_meta_read_picture(path, &img, &len)) {
        ok = decode_image(img, len, out_rgba, out_w, out_h);
    }
    free(img);
    return ok;
}

/* Album folders (common for FLAC rips) often carry the cover as a file. */
static int load_folder_art(const char *track_path, unsigned char **out_rgba, int *out_w, int *out_h)
{
    static const char *names[] = {
        "cover.jpg", "cover.png", "folder.jpg", "folder.png",
        "front.jpg", "front.png", "Cover.jpg", "Folder.jpg", "Front.jpg", "AlbumArt.jpg"
    };
    char path[1100];
    const char *slash = strrchr(track_path, '/');
    int dir_len = slash ? (int)(slash - track_path) : 1;
    const char *dir = slash ? track_path : ".";
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        int w, h, n;
        unsigned char *rgba;
        snprintf(path, sizeof(path), "%.*s/%s", dir_len, dir, names[i]);
        rgba = stbi_load(path, &w, &h, &n, 4);
        if (rgba && w > 0 && h > 0) {
            *out_rgba = rgba;
            *out_w = w;
            *out_h = h;
            return 1;
        }
        stbi_image_free(rgba);
    }
    return 0;
}

int album_art_load(const char *path, unsigned char **out_rgba, int *out_w, int *out_h)
{
    const char *dot;

    if (!path || !out_rgba || !out_w || !out_h) {
        return 0;
    }
    *out_rgba = NULL;
    *out_w = 0;
    *out_h = 0;
    dot = strrchr(path, '.');
    if (dot && strcasecmp(dot, ".flac") == 0) {
        if (load_flac_art(path, out_rgba, out_w, out_h)) {
            return 1;
        }
    } else if (load_id3_art(path, out_rgba, out_w, out_h)) {
        return 1;
    }
    return load_folder_art(path, out_rgba, out_w, out_h);
}
