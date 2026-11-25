// PicoSing - karaoke for the MSX PicoVerse 2350
// library.h - the song list (filled by the platform's card scan)

#ifndef LIBRARY_H
#define LIBRARY_H

#include <stdint.h>
#include <stdbool.h>

#define LIB_MAX       400
#define LIB_POOL      (24 * 1024)

typedef struct {
    char     artist[32];
    char     title[40];
    uint8_t  duet;
    uint16_t path;      // offset of the .txt path in the pool
} lib_entry_t;

void lib_clear(void);
// artist/title in Latin-1; path as the platform wants it (FatFs path on the Pico)
bool lib_add(const char *artist, const char *title, bool duet, const char *path);
void lib_sort(void);            // by artist, then title (case-insensitive)
int  lib_count(void);
void lib_remove(int i);          // its path stays in the pool until the next scan
const lib_entry_t *lib_get(int i);
const char *lib_path(int i);

#endif
