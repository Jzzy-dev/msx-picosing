// PicoSing - karaoke for the MSX PicoVerse 2350
// screen_link.h - the Pico side of the display list handshake
//
// The MSX writes the sequence number of the last list it ran to 7F81h to
// ask for the next one. A new list is only built when that number is the
// newest one: so a list is never rewritten while the MSX may still be
// running it (it asks again after a timeout when the Pico was busy).
//
// Self-repair: one row of the screen is sent again with every list (the
// whole screen every ~4 s), and a new screen (menu, singing, results) is
// sent in full, so a lost VRAM write never stays.
//
// Plain C: the firmware (main.c) and the PC tests (tests/e2e.c) use it.

#ifndef SCREEN_LINK_H
#define SCREEN_LINK_H

#include <stdint.h>
#include <stdbool.h>
#include "fb.h"

#define LINK_BUDGET 6000

// From the bus side (core 0): the MSX wrote 'data' to 'addr'.
void link_on_write(uint16_t addr, uint8_t data);

// From the game side (core 1): build the next list in 'window' (the 16 KB
// at 8000h) when one was asked for. Returns true when a list was written.
bool link_service(uint8_t *window);

// Tests: turn the handshake check off (the old behaviour)
extern bool link_strict;

#endif
