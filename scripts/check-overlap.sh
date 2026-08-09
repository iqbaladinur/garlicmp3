#!/bin/bash
# Overlap detector for garlicmp3 UI: hijacks every draw call in ui_sdl.c,
# records bounding boxes, reports text/text overlaps and text outside shell.
# No vision needed — deterministic. Exit 0 if clean, 1 if overlaps found.
set -e

REPO="$(cd "$(dirname "$0")/.." && pwd)"

podman run --rm \
  -v "$REPO:/src:ro" \
  -v sdl15-preview:/tmp/sdl \
  localhost/aveferrum/rg35xx-toolchain:latest sh -c '
set -e
echo "== renaming primitives to track_* =="
sed -e "s/^static void fill_round_rect(/static void track_fill_round_rect_orig(/" \
    -e "s/^static void fill_circle(/static void track_fill_circle_orig(/" \
    -e "s/^static void fill_rect(/static void track_fill_rect_orig(/" \
    -e "s/^static void draw_char_scaled(/static void track_draw_char_scaled_orig(/" \
    -e "s/^static void draw_char(/static void track_draw_char_orig(/" \
    -e "s/^static void draw_text_scaled(/static void track_draw_text_scaled_orig(/" \
    -e "s/^static void draw_text_right(/static void track_draw_text_right_orig(/" \
    -e "s/^static void draw_marquee_text(/static void track_draw_marquee_text_orig(/" \
    -e "s/^static void draw_text(/static void track_draw_text_orig(/" \
    -e "s/fill_round_rect(/track_fill_round_rect(/g" \
    -e "s/fill_circle(/track_fill_circle(/g" \
    -e "s/fill_rect(/track_fill_rect(/g" \
    -e "s/draw_char_scaled(/track_draw_char_scaled(/g" \
    -e "s/draw_char(/track_draw_char(/g" \
    -e "s/draw_text_scaled(/track_draw_text_scaled(/g" \
    -e "s/draw_text_right(/track_draw_text_right(/g" \
    -e "s/draw_marquee_text(/track_draw_marquee_text(/g" \
    -e "s/draw_text(/track_draw_text(/g" \
    /src/src/ui_sdl.c > /tmp/ui_sdl_track.c
echo "== compiling =="
gcc -O2 -Wno-unused-function -o /tmp/overlap /src/tools/overlap.c /src/src/settings.c \
  -I/tmp/sdl/include -I/src/src -I/tmp -L/tmp/sdl/lib -lSDL -lm
echo "== running =="
cd /tmp && SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy /tmp/overlap
'
