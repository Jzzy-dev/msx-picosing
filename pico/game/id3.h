// PicoSing - karaoke for the MSX PicoVerse 2350
// id3.h - an MP3's title, artist, album and year: read and change them

#ifndef PICOSING_ID3_H
#define PICOSING_ID3_H

#include <stdbool.h>

typedef struct {
    char title[48];
    char artist[48];
    char album[48];
    char year[8];
    bool had_v2, had_v1;
} id3_text_t;

// From the ID3v2 tag, else the ID3v1 tag (display text, Latin-1)
bool id3_read(const char *path, id3_text_t *t);
// NULL when done, else what went wrong (for the screen)
const char *id3_write(const char *path, const id3_text_t *t);

#endif
