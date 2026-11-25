// PicoSing - karaoke for the MSX PicoVerse 2350
// singers.h - singers (names and avatars), the session's scores and the
// score history per song
//
// On the card, in the data folder (plat_data_dir()):
//   SINGERS.TXT  one singer a line: "<avatar>,<name>"
//   SCORES.TXT   one sung song a line: "<score>\t<singer>\t<artist> - <title>"

#ifndef SINGERS_H
#define SINGERS_H

#include <stdint.h>
#include <stdbool.h>
#include "fb.h"

#define SINGERS_MAX   8
#define NAME_MAX      10
#define AVATARS       12
#define TOP_MAX       5

typedef struct {
    char    name[NAME_MAX + 1];
    uint8_t avatar;
    // this session
    int     songs, total, best;
} singer_t;

typedef struct { int score; char name[NAME_MAX + 1]; } top_t;

void singers_load(void);            // from the card (or two default singers)
void singers_save(void);
int  singers_count(void);
singer_t *singer_get(int i);
bool singer_add(void);              // a new singer at the end (default name)
void singer_remove(int i);

// A song was sung: add to the session and to SCORES.TXT
void singers_record(int singer, int score, const char *artist, const char *title);
// The best scores ever for a song (from SCORES.TXT); returns how many
int  singers_top(const char *artist, const char *title, top_t *top, int max);

// Draw avatar a (0..AVATARS-1) at x, y: 16 x 16 pixels, scale 1 or 2
void avatar_draw(fb_t *f, int a, int x, int y, int scale);

#endif
