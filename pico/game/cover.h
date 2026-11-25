// PicoSing - karaoke for the MSX PicoVerse 2350
// cover.h - a song's cover as a 96 x 96 SCREEN 5 picture
//
// A baseline JPEG (a file in the song's folder, or the picture in the MP3's
// ID3 tag) is decoded with TJpgDec (scaled while decoding), its own colours
// are chosen for the palette entries the screen leaves free (k-means, in the
// V9938's 512 colours), and it is dithered (Floyd-Steinberg) with all 16.

#ifndef COVER_H
#define COVER_H

#include <stdint.h>
#include <stdbool.h>
#include "fb.h"

#define COVER_PX 96

typedef struct {
    bool    ok;
    uint8_t pal[32];                      // the screen's palette with the cover's colours
    uint8_t idx[COVER_PX * COVER_PX];     // palette entries
} cover_t;

// base: the screen's palette; free_mask: the entries (bits) the cover may set.
// off/len: where the JPEG is in the file (0, 0 = the whole file).
bool cover_load(cover_t *c, const char *path, uint32_t off, uint32_t len,
                const uint8_t base[32], uint16_t free_mask);
void cover_draw(fb_t *f, const cover_t *c, int x, int y);

#endif
