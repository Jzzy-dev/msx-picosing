// PicoSing - karaoke for the MSX PicoVerse 2350
// dlist.h - display lists: what the MSX must do to its SCREEN 5 so that it
// shows the Pico's picture
//
// The Pico keeps two pictures: the one it wants on screen and the one the
// MSX shows. dlist_build() compares them and writes the changed areas as
// VDP operations, until the list is full; what does not fit goes in the
// next list. The MSX ROM (msx/picosing.asm) runs them.
//
// List layout (in the 16 KB window at 8000h):
//   +0  seq      u8, changes with every new list (written last)
//   +1  length   u16, bytes of operations
//   +3  flags    u8, DL_FLAG_*
//   +4  operations:
//       01 x y w h c          FILL: bytes x..x+w-1 of rows y..y+h-1 with byte c (HMMV)
//       02 x y w h data[w*h]  COPY: those bytes from the list (CPU writes to VRAM)
//       03 pal[32]            PALETTE: 16 colours, V9938 format
//       00                    END
//   x and w count bytes (2 pixels each), w 1..128, h 1..212.

#ifndef DLIST_H
#define DLIST_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "fb.h"

#define DL_OP_END     0x00
#define DL_OP_FILL    0x01
#define DL_OP_COPY    0x02
#define DL_OP_PALETTE 0x03

#define DL_HEADER     4
#define DL_FLAG_MORE  0x01     // the screen is not complete yet

// Append the changes (at most 'budget' bytes of operations) to out+DL_HEADER.
// Updates 'shown' for what was written. Returns the bytes of operations,
// END included; sets *more when changes are left for the next list.
size_t dlist_build(const fb_t *want, fb_t *shown, uint8_t *ops, size_t budget,
                   const uint8_t *palette, bool *more);

// The MSX side, in C: apply a list to a picture (for tests and the PC preview).
void dlist_apply(const uint8_t *ops, size_t len, fb_t *screen, uint8_t *palette);

#endif
